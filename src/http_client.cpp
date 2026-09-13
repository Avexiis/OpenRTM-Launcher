#include "http_client.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace openrtm
{
namespace
{
std::wstring widen(const std::string_view value)
{
    if (value.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
    {
        throw std::runtime_error("A URL is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::runtime_error windowsError(const std::string_view operation)
{
    return std::runtime_error(std::string(operation) + " failed (Windows error "
        + std::to_string(GetLastError()) + ")");
}

struct InternetHandleCloser
{
    void operator()(void* handle) const
    {
        if (handle != nullptr)
        {
            WinHttpCloseHandle(handle);
        }
    }
};

using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;
using DataSink = std::function<bool(const char*, std::size_t)>;

std::optional<std::uint64_t> contentLength(HINTERNET request)
{
    std::array<wchar_t, 64> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
            buffer.data(), &size, WINHTTP_NO_HEADER_INDEX))
    {
        return std::nullopt;
    }
    try
    {
        return std::stoull(buffer.data());
    }
    catch (...)
    {
        return std::nullopt;
    }
}

void performRequest(const std::string& url, const DataSink& sink,
    const ProgressCallback& progress)
{
    const std::wstring wideUrl = widen(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components))
    {
        throw windowsError("Parsing the download URL");
    }

    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS)
    {
        throw std::runtime_error("Only HTTP and HTTPS download URLs are supported");
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring resource(components.lpszUrlPath, components.dwUrlPathLength);
    resource.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    if (resource.empty())
    {
        resource = L"/";
    }

    const std::wstring userAgent = widen("OpenRTM Launcher/" OPENRTM_LAUNCHER_VERSION);
    InternetHandle session(WinHttpOpen(userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session)
    {
        throw windowsError("Starting WinHTTP");
    }
    WinHttpSetTimeouts(session.get(), 15000, 15000, 15000, 60000);

    InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), components.nPort, 0));
    if (!connection)
    {
        throw windowsError("Connecting to the update server");
    }

    const DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", resource.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request)
    {
        throw windowsError("Creating the update request");
    }

    constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\n"
                                  L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1L),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request.get(), nullptr))
    {
        throw windowsError("Downloading from the update server");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
    {
        throw windowsError("Reading the update server response");
    }
    if (status < 200 || status >= 300)
    {
        throw std::runtime_error("The update server returned HTTP " + std::to_string(status));
    }

    const auto total = contentLength(request.get());
    std::uint64_t received = 0;
    while (true)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available))
        {
            throw windowsError("Reading update data");
        }
        if (available == 0)
        {
            break;
        }

        std::vector<char> buffer(available);
        DWORD count = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), available, &count))
        {
            throw windowsError("Reading update data");
        }
        if (count == 0)
        {
            break;
        }
        if (!sink(buffer.data(), count))
        {
            throw std::runtime_error("The update response could not be written");
        }
        received += count;
        if (progress)
        {
            progress(received, total);
        }
    }
}
}

std::string HttpClient::getText(const std::string& url, const std::size_t maximumBytes) const
{
    std::string result;
    bool tooLarge = false;
    try
    {
        performRequest(url, [&](const char* data, const std::size_t size) {
            if (size > maximumBytes - result.size())
            {
                tooLarge = true;
                return false;
            }
            result.append(data, size);
            return true;
        }, {});
    }
    catch (...)
    {
        if (tooLarge)
        {
            throw std::runtime_error("The update response was unexpectedly large");
        }
        throw;
    }
    return result;
}

void HttpClient::download(const std::string& url, const std::filesystem::path& destination,
    const ProgressCallback& progress) const
{
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("Could not create " + destination.string());
    }
    performRequest(url, [&](const char* data, const std::size_t size) {
        output.write(data, static_cast<std::streamsize>(size));
        return output.good();
    }, progress);
    output.close();
    if (!output)
    {
        throw std::runtime_error("Could not finish writing " + destination.string());
    }
}
}

#else

#include <curl/curl.h>

namespace openrtm
{
namespace
{
using DataSink = std::function<bool(const char*, std::size_t)>;

struct CurlGlobal
{
    CurlGlobal()
    {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        {
            throw std::runtime_error("Could not initialize the network library");
        }
    }

    ~CurlGlobal()
    {
        curl_global_cleanup();
    }
};

struct Transfer
{
    DataSink sink;
    ProgressCallback progress;
    std::string callbackError;
};

std::size_t writeCallback(char* data, const std::size_t size, const std::size_t count, void* userData)
{
    auto& transfer = *static_cast<Transfer*>(userData);
    if (count != 0 && size > std::numeric_limits<std::size_t>::max() / count)
    {
        transfer.callbackError = "The update response was too large";
        return 0;
    }
    const std::size_t bytes = size * count;
    try
    {
        if (!transfer.sink(data, bytes))
        {
            transfer.callbackError = "The update response could not be written";
            return 0;
        }
    }
    catch (const std::exception& error)
    {
        transfer.callbackError = error.what();
        return 0;
    }
    return bytes;
}

int progressCallback(void* userData, const curl_off_t total, const curl_off_t current,
    curl_off_t, curl_off_t)
{
    auto& transfer = *static_cast<Transfer*>(userData);
    if (!transfer.progress)
    {
        return 0;
    }
    try
    {
        transfer.progress(static_cast<std::uint64_t>(current),
            total > 0 ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(total))
                      : std::nullopt);
        return 0;
    }
    catch (const std::exception& error)
    {
        transfer.callbackError = error.what();
        return 1;
    }
}

std::optional<std::filesystem::path> certificateBundle()
{
    constexpr std::array<std::string_view, 5> candidates = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
        "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        "/etc/ssl/cert.pem"
    };
    for (const auto candidate : candidates)
    {
        if (std::filesystem::is_regular_file(candidate))
        {
            return std::filesystem::path(candidate);
        }
    }
    return std::nullopt;
}

void performRequest(const std::string& url, const DataSink& sink,
    const ProgressCallback& progress, const long timeoutSeconds)
{
    static CurlGlobal global;
    (void)global;

    CURL* rawHandle = curl_easy_init();
    if (rawHandle == nullptr)
    {
        throw std::runtime_error("Could not initialize an HTTP request");
    }
    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(rawHandle, curl_easy_cleanup);

    curl_slist* rawHeaders = nullptr;
    rawHeaders = curl_slist_append(rawHeaders, "Accept: application/vnd.github+json");
    rawHeaders = curl_slist_append(rawHeaders, "X-GitHub-Api-Version: 2022-11-28");
    const std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(
        rawHeaders, curl_slist_free_all);

    Transfer transfer{sink, progress, {}};
    std::array<char, CURL_ERROR_SIZE> errorBuffer{};
    curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT,
        "OpenRTM Launcher/" OPENRTM_LAUNCHER_VERSION);
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(handle.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(handle.get(), CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, progressCallback);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, progress ? 0L : 1L);
    curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER, errorBuffer.data());

    const auto caBundle = certificateBundle();
    const std::string caBundleName = caBundle ? caBundle->string() : std::string();
    if (!caBundleName.empty())
    {
        curl_easy_setopt(handle.get(), CURLOPT_CAINFO, caBundleName.c_str());
    }

    const CURLcode result = curl_easy_perform(handle.get());
    if (result != CURLE_OK)
    {
        if (!transfer.callbackError.empty())
        {
            throw std::runtime_error(transfer.callbackError);
        }
        const std::string detail = errorBuffer[0] == '\0' ? curl_easy_strerror(result)
                                                           : errorBuffer.data();
        throw std::runtime_error("Download failed: " + detail);
    }
}
}

std::string HttpClient::getText(const std::string& url, const std::size_t maximumBytes) const
{
    std::string result;
    bool tooLarge = false;
    try
    {
        performRequest(url, [&](const char* data, const std::size_t size) {
            if (size > maximumBytes - result.size())
            {
                tooLarge = true;
                return false;
            }
            result.append(data, size);
            return true;
        }, {}, 30L);
    }
    catch (...)
    {
        if (tooLarge)
        {
            throw std::runtime_error("The update response was unexpectedly large");
        }
        throw;
    }
    return result;
}

void HttpClient::download(const std::string& url, const std::filesystem::path& destination,
    const ProgressCallback& progress) const
{
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        throw std::runtime_error("Could not create " + destination.string());
    }
    performRequest(url, [&](const char* data, const std::size_t size) {
        output.write(data, static_cast<std::streamsize>(size));
        return output.good();
    }, progress, 1800L);
    output.close();
    if (!output)
    {
        throw std::runtime_error("Could not finish writing " + destination.string());
    }
}
}

#endif
