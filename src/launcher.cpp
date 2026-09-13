#include "launcher.hpp"

#include "gui.hpp"
#include "http_client.hpp"
#include "launcher_log.hpp"
#include "launcher_services.hpp"
#include "openrtm/core.hpp"

#ifndef _WIN32
#include "openrtm_icon_png.hpp"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>

extern char** environ;
#endif

namespace openrtm
{
namespace
{
namespace fs = std::filesystem;

constexpr std::string_view defaultApiUrl =
    "https://api.github.com/repos/Avexiis/OpenRTM_Production/commits?path=OpenRTM.jar&per_page=1";
constexpr std::string_view defaultDownloadUrl =
    "https://media.githubusercontent.com/media/Avexiis/OpenRTM_Production/{commit}/OpenRTM.jar";

enum class Command
{
    launch,
    check,
    install,
    uninstall,
    help,
    version
};

struct Options
{
    Command command = Command::launch;
    bool forceUpdate = false;
    bool noLaunch = false;
    bool assumeYes = false;
    std::vector<std::string> jarArguments;
};

struct JavaInstallation
{
    fs::path home;
    fs::path java;
    int major = 0;
};

struct Paths
{
    fs::path dataDirectory;
    fs::path jar;
    fs::path commit;
    fs::path partialJar;
};

std::optional<std::string> environment(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return std::nullopt;
    }
    return std::string(value);
}

#ifdef _WIN32
std::wstring widen(const std::string_view value)
{
    if (value.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}

std::optional<fs::path> pathEnvironment(const wchar_t* name)
{
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
    {
        return std::nullopt;
    }
    std::wstring value(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0)
    {
        return std::nullopt;
    }
    value.resize(written);
    return fs::path(value);
}

std::wstring quoteWindowsArgument(const std::wstring& argument)
{
    if (argument.find_first_of(L" \t\"") == std::wstring::npos)
    {
        return argument;
    }

    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (const wchar_t character : argument)
    {
        if (character == L'\\')
        {
            ++slashes;
            continue;
        }
        if (character == L'\"')
        {
            result.append(slashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            slashes = 0;
            continue;
        }
        result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(character);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
#else
std::optional<fs::path> pathEnvironment(const char* name)
{
    const auto value = environment(name);
    return value ? std::optional<fs::path>(*value) : std::nullopt;
}
#endif

fs::path homeDirectory()
{
#ifdef _WIN32
    if (const auto profile = pathEnvironment(L"USERPROFILE"))
    {
        return *profile;
    }
    const auto drive = pathEnvironment(L"HOMEDRIVE");
    const auto path = pathEnvironment(L"HOMEPATH");
    if (drive && path)
    {
        return fs::path(drive->wstring() + path->wstring());
    }
#else
    if (const auto home = pathEnvironment("HOME"))
    {
        return *home;
    }
    if (const passwd* entry = getpwuid(getuid()); entry != nullptr && entry->pw_dir != nullptr)
    {
        return fs::path(entry->pw_dir);
    }
#endif
    throw std::runtime_error("Could not determine the current user's home directory");
}

fs::path configuredDataDirectory()
{
#ifdef _WIN32
    if (const auto overridePath = pathEnvironment(L"OPENRTM_HOME"))
#else
    if (const auto overridePath = pathEnvironment("OPENRTM_HOME"))
#endif
    {
        return fs::absolute(*overridePath).lexically_normal();
    }
    return (homeDirectory() / ".openrtm").lexically_normal();
}

Paths launcherPaths()
{
    Paths paths;
    paths.dataDirectory = configuredDataDirectory();
    paths.jar = paths.dataDirectory / "OpenRTM.jar";
    paths.commit = paths.dataDirectory / "commit.sha";
    paths.partialJar = paths.dataDirectory / "OpenRTM.jar.download";
    return paths;
}

std::string readTextFile(const fs::path& path, const std::size_t maximumBytes = 1024 * 1024)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) > maximumBytes)
    {
        return {};
    }
    input.seekg(0, std::ios::beg);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void replaceFile(const fs::path& source, const fs::path& destination)
{
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        throw std::runtime_error("Could not replace " + destination.string()
            + " (Windows error " + std::to_string(GetLastError()) + ")");
    }
#else
    std::error_code error;
    fs::rename(source, destination, error);
    if (error)
    {
        throw std::runtime_error("Could not replace " + destination.string() + ": "
            + error.message());
    }
#endif
}

void writeTextFileAtomically(const fs::path& path, const std::string_view contents)
{
    fs::path temporary = path;
    temporary += ".new";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not create " + temporary.string());
        }
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
        if (!output)
        {
            throw std::runtime_error("Could not write " + temporary.string());
        }
    }
    replaceFile(temporary, path);
}

#ifndef _WIN32
void writeBinaryFileAtomically(const fs::path& path, const unsigned char* data,
    const std::size_t size)
{
    fs::path temporary = path;
    temporary += ".new";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("Could not create " + temporary.string());
        }
        output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        output.flush();
        if (!output)
        {
            throw std::runtime_error("Could not write " + temporary.string());
        }
    }
    replaceFile(temporary, path);
}
#endif

std::optional<JavaInstallation> probeJavaHome(fs::path home)
{
    std::error_code error;
    home = fs::weakly_canonical(home, error);
    if (error)
    {
        return std::nullopt;
    }
#ifdef _WIN32
    const fs::path java = home / "bin" / "java.exe";
    const fs::path compiler = home / "bin" / "javac.exe";
#else
    const fs::path java = home / "bin" / "java";
    const fs::path compiler = home / "bin" / "javac";
#endif
    if (!fs::is_regular_file(java, error) || error || !fs::is_regular_file(compiler, error) || error)
    {
        return std::nullopt;
    }
    const auto major = javaMajorFromRelease(readTextFile(home / "release", 128 * 1024));
    if (!major || *major < 17)
    {
        return std::nullopt;
    }
    return JavaInstallation{std::move(home), java, *major};
}

void addCandidate(std::vector<fs::path>& candidates, const fs::path& value)
{
    if (!value.empty())
    {
        candidates.push_back(value);
    }
}

void addCandidateFromExecutable(std::vector<fs::path>& candidates, const fs::path& executable)
{
    std::error_code error;
    const fs::path resolved = fs::weakly_canonical(executable, error);
    if (!error && resolved.has_parent_path() && resolved.parent_path().has_parent_path())
    {
        addCandidate(candidates, resolved.parent_path().parent_path());
    }
}

void addPathCandidates(std::vector<fs::path>& candidates)
{
#ifdef _WIN32
    const auto pathValue = pathEnvironment(L"PATH");
    constexpr wchar_t separator = L';';
    constexpr wchar_t javaName[] = L"java.exe";
    if (!pathValue)
    {
        return;
    }
    const std::wstring value = pathValue->wstring();
    std::size_t start = 0;
    while (start <= value.size())
    {
        const std::size_t end = value.find(separator, start);
        std::wstring entry = value.substr(start, end == std::wstring::npos ? end : end - start);
        if (entry.size() >= 2 && entry.front() == L'\"' && entry.back() == L'\"')
        {
            entry = entry.substr(1, entry.size() - 2);
        }
        addCandidateFromExecutable(candidates, fs::path(entry) / javaName);
        if (end == std::wstring::npos)
        {
            break;
        }
        start = end + 1;
    }
#else
    const auto pathValue = environment("PATH");
    if (!pathValue)
    {
        return;
    }
    std::size_t start = 0;
    while (start <= pathValue->size())
    {
        const std::size_t end = pathValue->find(':', start);
        const std::string entry = pathValue->substr(start,
            end == std::string::npos ? end : end - start);
        addCandidateFromExecutable(candidates, fs::path(entry) / "java");
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
#endif
}

void scanJavaRoot(std::vector<fs::path>& candidates, const fs::path& root)
{
    std::error_code error;
    if (!fs::is_directory(root, error) || error)
    {
        return;
    }
    addCandidate(candidates, root);
    fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied,
        error);
    const fs::recursive_directory_iterator end;
    while (!error && iterator != end)
    {
        if (iterator.depth() >= 2)
        {
            iterator.disable_recursion_pending();
        }
        if (iterator->is_directory(error) && !error)
        {
            addCandidate(candidates, iterator->path());
        }
        iterator.increment(error);
    }
}

std::optional<JavaInstallation> findJava()
{
    std::vector<fs::path> candidates;
#ifdef _WIN32
    if (const auto javaHome = pathEnvironment(L"JAVA_HOME"))
#else
    if (const auto javaHome = pathEnvironment("JAVA_HOME"))
#endif
    {
        addCandidate(candidates, *javaHome);
    }
    addPathCandidates(candidates);

    const fs::path home = homeDirectory();
    scanJavaRoot(candidates, home / ".jdks");
#ifdef _WIN32
    std::vector<fs::path> programRoots;
    for (const wchar_t* variable : {L"ProgramW6432", L"ProgramFiles", L"ProgramFiles(x86)"})
    {
        if (const auto root = pathEnvironment(variable))
        {
            programRoots.push_back(*root);
        }
    }
    constexpr std::array<std::wstring_view, 7> vendors = {
        L"Java", L"Eclipse Adoptium", L"Microsoft", L"Zulu", L"BellSoft",
        L"Amazon Corretto", L"RedHat"
    };
    for (const auto& root : programRoots)
    {
        for (const auto vendor : vendors)
        {
            scanJavaRoot(candidates, root / vendor);
        }
    }
#else
    scanJavaRoot(candidates, home / ".sdkman" / "candidates" / "java");
    for (const fs::path& root : {fs::path("/usr/lib/jvm"), fs::path("/usr/lib64/jvm"),
             fs::path("/usr/java"), fs::path("/opt/java"), fs::path("/opt/jdk"),
             fs::path("/opt/jdks")})
    {
        scanJavaRoot(candidates, root);
    }
#endif

    std::set<fs::path> visited;
    std::optional<JavaInstallation> newestCompatible;
    for (const auto& candidate : candidates)
    {
        std::error_code error;
        const fs::path normalized = fs::weakly_canonical(candidate, error);
        if (error || !visited.insert(normalized).second)
        {
            continue;
        }
        const auto installation = probeJavaHome(normalized);
        if (!installation)
        {
            continue;
        }
        if (installation->major == 17)
        {
            return installation;
        }
        if (!newestCompatible || installation->major > newestCompatible->major)
        {
            newestCompatible = installation;
        }
    }
    return newestCompatible;
}

std::string jdkDownloadUrl()
{
#if defined(_WIN32)
    return "https://adoptium.net/temurin/releases/?version=17&os=windows&arch=x64&package=jdk";
#elif defined(__aarch64__)
    return "https://adoptium.net/temurin/releases/?version=17&os=linux&arch=aarch64&package=jdk";
#else
    return "https://adoptium.net/temurin/releases/?version=17&os=linux&arch=x64&package=jdk";
#endif
}

void reportMissingJava()
{
    const std::string url = jdkDownloadUrl();
    std::cerr << "OpenRTM needs JDK 17 or newer. No compatible JDK was found.\n"
              << "Download OpenJDK 17: " << url << '\n';
#ifdef _WIN32
    const int choice = MessageBoxW(nullptr,
        L"OpenRTM needs JDK 17 or newer, but no compatible JDK was found.\n\n"
        L"Open the Eclipse Temurin OpenJDK 17 download page?",
        L"OpenRTM Launcher", MB_YESNO | MB_ICONWARNING);
    if (choice == IDYES)
    {
        const std::wstring wideUrl = widen(url);
        ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
#endif
}

#ifndef _WIN32
fs::path currentExecutable()
{
    std::vector<char> buffer(1024);
    while (true)
    {
        const ssize_t count = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (count < 0)
        {
            throw std::runtime_error("Could not locate the launcher executable");
        }
        if (static_cast<std::size_t>(count) < buffer.size())
        {
            return fs::path(std::string(buffer.data(), static_cast<std::size_t>(count)));
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::string desktopQuote(const fs::path& path)
{
    std::string result = "\"";
    for (const char character : path.string())
    {
        if (character == '\\' || character == '\"' || character == '$' || character == '`')
        {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    result.push_back('\"');
    return result;
}

void installDesktop()
{
    const fs::path home = homeDirectory();
    const fs::path binaryDirectory = home / ".local" / "bin";
    const fs::path applicationsDirectory = home / ".local" / "share" / "applications";
    const fs::path iconsDirectory = home / ".local" / "share" / "icons" / "hicolor"
        / "256x256" / "apps";
    const fs::path installedBinary = binaryDirectory / "openrtm";
    const fs::path desktopFile = applicationsDirectory / "openrtm.desktop";
    const fs::path iconFile = iconsDirectory / "openrtm.png";
    fs::create_directories(binaryDirectory);
    fs::create_directories(applicationsDirectory);
    fs::create_directories(iconsDirectory);

    fs::path temporaryBinary = installedBinary;
    temporaryBinary += ".new";
    fs::copy_file(currentExecutable(), temporaryBinary, fs::copy_options::overwrite_existing);
    fs::permissions(temporaryBinary,
        fs::perms::owner_read | fs::perms::owner_write | fs::perms::owner_exec
            | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read
            | fs::perms::others_exec,
        fs::perm_options::replace);
    replaceFile(temporaryBinary, installedBinary);

    const std::string executable = desktopQuote(installedBinary);
    const std::string contents =
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=OpenRTM\n"
        "Comment=Launch and update OpenRTM\n"
        "Exec=" + executable + "\n"
        "TryExec=" + installedBinary.string() + "\n"
        "Icon=openrtm\n"
        "Terminal=false\n"
        "Categories=Utility;Game;\n"
        "StartupNotify=true\n";
    writeTextFileAtomically(desktopFile, contents);
    writeBinaryFileAtomically(iconFile, openrtm_icon_png, openrtm_icon_png_len);
    fs::permissions(desktopFile,
        fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read
            | fs::perms::others_read,
        fs::perm_options::replace);
    std::cout << "Installed OpenRTM to " << installedBinary << "\n"
              << "Desktop entry: " << desktopFile << '\n';
}

void removeDesktopIntegration()
{
    const fs::path home = homeDirectory();
    std::error_code error;
    fs::remove(home / ".local" / "share" / "applications" / "openrtm.desktop", error);
    error.clear();
    fs::remove(home / ".local" / "share" / "icons" / "hicolor" / "256x256" / "apps"
            / "openrtm.png",
        error);
    error.clear();
    fs::remove(home / ".local" / "bin" / "openrtm", error);
}
#endif

class UpdateLock
{
public:
    explicit UpdateLock(const fs::path& path)
    {
#ifdef _WIN32
        for (int attempt = 0; attempt < 600; ++attempt)
        {
            handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE)
            {
                return;
            }
            if (GetLastError() != ERROR_SHARING_VIOLATION)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        throw std::runtime_error("Could not acquire the OpenRTM update lock");
#else
        descriptor_ = open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (descriptor_ < 0 || flock(descriptor_, LOCK_EX) != 0)
        {
            if (descriptor_ >= 0)
            {
                close(descriptor_);
                descriptor_ = -1;
            }
            throw std::runtime_error("Could not acquire the OpenRTM update lock");
        }
#endif
    }

    UpdateLock(const UpdateLock&) = delete;
    UpdateLock& operator=(const UpdateLock&) = delete;

    ~UpdateLock()
    {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }
#else
        if (descriptor_ >= 0)
        {
            flock(descriptor_, LOCK_UN);
            close(descriptor_);
        }
#endif
    }

private:
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int descriptor_ = -1;
#endif
};

class ProgressReporter
{
public:
    void update(const std::uint64_t current, const std::optional<std::uint64_t> total)
    {
        if (total && *total > 0)
        {
            const int percentage = static_cast<int>(std::min<std::uint64_t>(100,
                current * 100 / *total));
            if (percentage / 5 == lastPercentage_ / 5)
            {
                return;
            }
            lastPercentage_ = percentage;
            printed_ = true;
            std::cout << "\rDownloading OpenRTM.jar: " << percentage << "% ("
                      << current / (1024 * 1024) << " / " << *total / (1024 * 1024)
                      << " MiB)" << std::flush;
            return;
        }
        if (current < lastBytes_ + 16 * 1024 * 1024)
        {
            return;
        }
        lastBytes_ = current;
        printed_ = true;
        std::cout << "\rDownloading OpenRTM.jar: " << current / (1024 * 1024) << " MiB"
                  << std::flush;
    }

    void finish()
    {
        if (printed_)
        {
            std::cout << '\n';
        }
    }

private:
    int lastPercentage_ = -10;
    std::uint64_t lastBytes_ = 0;
    bool printed_ = false;
};

std::string latestCommit(const HttpClient& client)
{
    const std::string apiUrl = environment("OPENRTM_API_URL").value_or(
        std::string(defaultApiUrl));
    const auto hash = commitHashFromJson(client.getText(apiUrl));
    if (!hash || !isValidCommitHash(*hash))
    {
        throw std::runtime_error("GitHub did not return a valid OpenRTM commit hash");
    }
    return *hash;
}

std::string cachedCommit(const Paths& paths)
{
    const std::string hash = trim(readTextFile(paths.commit, 1024));
    return isValidCommitHash(hash) ? hash : std::string();
}

void downloadUpdate(const HttpClient& client, const Paths& paths, const std::string& commit,
    const ProgressCallback& progress)
{
    const std::string urlTemplate = environment("OPENRTM_DOWNLOAD_URL_TEMPLATE").value_or(
        std::string(defaultDownloadUrl));
    const std::string url = downloadUrlForCommit(urlTemplate, commit);
    std::error_code error;
    fs::remove(paths.partialJar, error);

    try
    {
        client.download(url, paths.partialJar, progress);
        if (!isJarArchive(paths.partialJar))
        {
            throw std::runtime_error("The downloaded file is not a valid JAR archive");
        }
        replaceFile(paths.partialJar, paths.jar);
        writeTextFileAtomically(paths.commit, commit + "\n");
    }
    catch (...)
    {
        error.clear();
        fs::remove(paths.partialJar, error);
        throw;
    }
}

void ensureLatestJar(const Paths& paths, const bool forceUpdate, const StatusCallback& status,
    const ProgressCallback& progress)
{
    HttpClient client;
    const bool hasCachedJar = isJarArchive(paths.jar);
    try
    {
        const std::string remoteCommit = latestCommit(client);
        const std::string localCommit = cachedCommit(paths);
        if (!forceUpdate && hasCachedJar && localCommit == remoteCommit)
        {
            status("OpenRTM is current (" + remoteCommit.substr(0, 12) + ")");
            return;
        }

        status(hasCachedJar ? "Downloading the latest OpenRTM update"
                            : "Downloading OpenRTM for the first time");
        downloadUpdate(client, paths, remoteCommit, progress);
        status("Installed OpenRTM " + remoteCommit.substr(0, 12));
    }
    catch (const std::exception& error)
    {
        if (hasCachedJar)
        {
            status("Update check failed; using the installed OpenRTM");
            return;
        }
        throw;
    }
}

int checkStatus(const Paths& paths, const JavaInstallation& java)
{
    std::cout << "JDK: " << java.home << " (Java " << java.major << ")\n";
    const std::string local = cachedCommit(paths);
    std::cout << "Cached JAR: " << (isJarArchive(paths.jar) ? "present" : "missing") << '\n';
    std::cout << "Cached commit: " << (local.empty() ? "unknown" : local) << '\n';
    const std::string remote = latestCommit(HttpClient{});
    std::cout << "Latest commit: " << remote << '\n';
    std::cout << "Update available: "
              << ((!isJarArchive(paths.jar) || local != remote) ? "yes" : "no") << '\n';
    return 0;
}

void launchJava(const JavaInstallation& installation, const Paths& paths,
    const std::vector<std::string>& arguments)
{
    std::cout << "Starting OpenRTM...\n" << std::flush;
#ifdef _WIN32
    fs::path java = installation.home / "bin" / "javaw.exe";
    std::error_code error;
    if (!fs::is_regular_file(java, error))
    {
        java = installation.java;
    }
    std::wstring command = quoteWindowsArgument(java.wstring()) + L" -jar "
        + quoteWindowsArgument(paths.jar.wstring());
    for (const auto& argument : arguments)
    {
        command += L" " + quoteWindowsArgument(widen(argument));
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
            CREATE_NEW_PROCESS_GROUP, nullptr, paths.dataDirectory.c_str(), &startup, &process))
    {
        throw std::runtime_error("Could not start Java (Windows error "
            + std::to_string(GetLastError()) + ")");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    std::vector<std::string> values;
    values.push_back(installation.java.string());
    values.push_back("-jar");
    values.push_back(paths.jar.string());
    values.insert(values.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(values.size() + 1);
    for (auto& value : values)
    {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);
    if (chdir(paths.dataDirectory.c_str()) != 0)
    {
        throw std::runtime_error("Could not enter " + paths.dataDirectory.string());
    }
    execv(installation.java.c_str(), argv.data());
    throw std::runtime_error("Could not start Java");
#endif
}

void launchJavaDetached(const JavaInstallation& installation, const Paths& paths)
{
#ifdef _WIN32
    launchJava(installation, paths, {});
#else
    std::vector<std::string> values = {
        installation.java.string(), "-jar", paths.jar.string()
    };
    std::vector<char*> arguments;
    arguments.reserve(values.size() + 1);
    for (auto& value : values)
    {
        arguments.push_back(value.data());
    }
    arguments.push_back(nullptr);

    pid_t child = 0;
    const int result = posix_spawn(&child, installation.java.c_str(), nullptr, nullptr,
        arguments.data(), environ);
    if (result != 0)
    {
        throw std::runtime_error("Could not start Java: " + std::string(std::strerror(result)));
    }
#endif
}

void uninstall(const Paths& paths, const bool assumeYes)
{
    if (!assumeYes)
    {
        std::cout << "This will permanently remove " << paths.dataDirectory
                  << " and all OpenRTM settings stored there. Continue? [y/N] " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        if (answer != "y" && answer != "Y" && answer != "yes" && answer != "YES")
        {
            std::cout << "Uninstall canceled.\n";
            return;
        }
    }

    std::error_code error;
    const std::uintmax_t removed = fs::remove_all(paths.dataDirectory, error);
    if (error)
    {
        throw std::runtime_error("Could not completely remove " + paths.dataDirectory.string()
            + ": " + error.message());
    }
#ifndef _WIN32
    removeDesktopIntegration();
#endif
    std::cout << "Removed " << paths.dataDirectory << " (" << removed << " entries).\n";
}

void setCommand(Options& options, const Command command)
{
    if (options.command != Command::launch)
    {
        throw std::runtime_error("Only one command may be specified");
    }
    options.command = command;
}

Options parseOptions(const int argc, char* argv[])
{
    Options options;
    bool jarArguments = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (jarArguments)
        {
            options.jarArguments.push_back(argument);
        }
        else if (argument == "--")
        {
            jarArguments = true;
        }
        else if (argument == "--check")
        {
            setCommand(options, Command::check);
        }
        else if (argument == "--install")
        {
            setCommand(options, Command::install);
        }
        else if (argument == "--uninstall")
        {
            setCommand(options, Command::uninstall);
        }
        else if (argument == "--help" || argument == "-h")
        {
            setCommand(options, Command::help);
        }
        else if (argument == "--version")
        {
            setCommand(options, Command::version);
        }
        else if (argument == "--force-update")
        {
            options.forceUpdate = true;
        }
        else if (argument == "--no-launch")
        {
            options.noLaunch = true;
        }
        else if (argument == "--yes")
        {
            options.assumeYes = true;
        }
        else
        {
            throw std::runtime_error("Unknown option: " + argument + " (use --help)");
        }
    }
    return options;
}

void printHelp()
{
    std::cout
        << "OpenRTM Launcher " OPENRTM_LAUNCHER_VERSION "\n\n"
        << "Usage: openrtm [options] [-- OpenRTM arguments]\n\n"
        << "  --check          Check Java and update status without downloading\n"
        << "  --force-update   Download OpenRTM.jar even when the commit matches\n"
        << "  --no-launch      Update OpenRTM.jar without starting it\n"
        << "  --install        Install Linux desktop integration\n"
        << "  --uninstall      Remove OpenRTM data and Linux desktop integration\n"
        << "  --yes            Skip the uninstall confirmation\n"
        << "  --version        Show the launcher version\n"
        << "  --help, -h       Show this help\n";
}

#ifdef _WIN32
void showErrorDialog(const std::string& message)
{
    const std::wstring text = widen(message);
    MessageBoxW(nullptr, text.c_str(), L"OpenRTM Launcher", MB_OK | MB_ICONERROR);
}
#endif
}

LauncherSnapshot inspectLauncherStatus()
{
    const Paths paths = launcherPaths();
    LauncherSnapshot snapshot;
    snapshot.dataDirectory = paths.dataDirectory;
    snapshot.localCommit = cachedCommit(paths);

    std::error_code error;
    snapshot.jarAvailable = isJarArchive(paths.jar);
    if (snapshot.jarAvailable)
    {
        snapshot.jarBytes = fs::file_size(paths.jar, error);
        if (error)
        {
            snapshot.jarBytes = 0;
        }
    }

    if (const auto java = findJava())
    {
        snapshot.javaAvailable = true;
        snapshot.javaHome = java->home;
        snapshot.javaMajor = java->major;
    }

#ifndef _WIN32
    snapshot.desktopInstalled = fs::is_regular_file(
        homeDirectory() / ".local" / "share" / "applications" / "openrtm.desktop", error);
#endif

    try
    {
        snapshot.remoteCommit = latestCommit(HttpClient{});
    }
    catch (const std::exception& caught)
    {
        snapshot.updateError = caught.what();
    }
    return snapshot;
}

void prepareLatestForGui(const bool forceUpdate, const StatusCallback& status,
    const ProgressCallback& progress)
{
    if (!findJava())
    {
        throw std::runtime_error("JDK 17 or newer is required");
    }

    const Paths paths = launcherPaths();
    const bool created = fs::create_directories(paths.dataDirectory);
#ifndef _WIN32
    if (created)
    {
        fs::permissions(paths.dataDirectory, fs::perms::owner_all, fs::perm_options::replace);
    }
#else
    (void)created;
#endif
    const StatusCallback report = [status](const std::string& message) {
        writeLauncherLog(message);
        if (status)
        {
            status(message);
        }
    };
    UpdateLock lock(paths.dataDirectory / ".update.lock");
    ensureLatestJar(paths, forceUpdate, report, progress);
}

void launchForGui()
{
    const auto java = findJava();
    if (!java)
    {
        throw std::runtime_error("JDK 17 or newer is required");
    }
    const Paths paths = launcherPaths();
    if (!isJarArchive(paths.jar))
    {
        throw std::runtime_error("OpenRTM.jar is not installed");
    }
    writeLauncherLog("Starting OpenRTM.jar");
    launchJavaDetached(*java, paths);
}

void uninstallForGui()
{
    writeLauncherLog("Removing OpenRTM application data");
    uninstall(launcherPaths(), true);
    disableLauncherLog();
}

void installDesktopForGui()
{
#ifdef _WIN32
    throw std::runtime_error("Desktop installation is only available on Linux");
#else
    installDesktop();
#endif
}

std::string jdkDownloadUrlForGui()
{
    return jdkDownloadUrl();
}

void openExternalUrl(const std::string& url)
{
#ifdef _WIN32
    const std::wstring wideUrl = widen(url);
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32)
    {
        throw std::runtime_error("Could not open the web browser");
    }
#else
    std::array<char*, 3> arguments = {
        const_cast<char*>("xdg-open"), const_cast<char*>(url.c_str()), nullptr
    };
    pid_t child = 0;
    const int result = posix_spawnp(&child, "xdg-open", nullptr, nullptr, arguments.data(), environ);
    if (result != 0)
    {
        throw std::runtime_error("Could not open the web browser: "
            + std::string(std::strerror(result)));
    }
#endif
}

int runLauncher(const int argc, char* argv[])
{
    try
    {
        const Paths paths = launcherPaths();
        initializeLauncherLog(paths.dataDirectory);
        if (argc == 1)
        {
            return runGraphicalLauncher(argc, argv);
        }

        const Options options = parseOptions(argc, argv);
        if (options.command == Command::help)
        {
            printHelp();
            return 0;
        }
        if (options.command == Command::version)
        {
            std::cout << "OpenRTM Launcher " OPENRTM_LAUNCHER_VERSION "\n";
            return 0;
        }

        if (options.command == Command::uninstall)
        {
            writeLauncherLog("Removing OpenRTM application data");
            uninstall(paths, options.assumeYes);
            disableLauncherLog();
            return 0;
        }
        if (options.command == Command::install)
        {
#ifdef _WIN32
            throw std::runtime_error("--install is only needed on Linux; run the EXE directly");
#else
            writeLauncherLog("Installing Linux desktop integration");
            installDesktop();
            return 0;
#endif
        }

        const auto java = findJava();
        if (!java)
        {
            writeLauncherLog("JDK 17 or newer was not found");
            reportMissingJava();
            return 2;
        }
        if (options.command == Command::check)
        {
            writeLauncherLog("Checking Java and OpenRTM status from the command line");
            return checkStatus(paths, *java);
        }

        const bool created = fs::create_directories(paths.dataDirectory);
#ifndef _WIN32
        if (created)
        {
            fs::permissions(paths.dataDirectory, fs::perms::owner_all, fs::perm_options::replace);
        }
#else
        (void)created;
#endif
        {
            UpdateLock lock(paths.dataDirectory / ".update.lock");
            ProgressReporter reporter;
            try
            {
                ensureLatestJar(paths, options.forceUpdate,
                    [](const std::string& message) { std::cout << message << ".\n"; },
                    [&](const std::uint64_t current,
                        const std::optional<std::uint64_t> total) {
                        reporter.update(current, total);
                    });
                reporter.finish();
            }
            catch (...)
            {
                reporter.finish();
                throw;
            }
        }
        if (!options.noLaunch)
        {
            writeLauncherLog("Starting OpenRTM.jar");
            launchJava(*java, paths, options.jarArguments);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        const std::string message = std::string("OpenRTM Launcher: ") + error.what();
        writeLauncherLog(message);
        std::cerr << message << '\n';
#ifdef _WIN32
        showErrorDialog(message);
#endif
        return 1;
    }
}
}
