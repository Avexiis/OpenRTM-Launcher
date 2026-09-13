#include "openrtm/core.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>

namespace openrtm
{
std::optional<int> javaMajorFromRelease(const std::string_view contents)
{
    static const std::regex pattern(R"re(JAVA_VERSION\s*=\s*"?([0-9]+))re");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(contents.begin(), contents.end(), match, pattern))
    {
        return std::nullopt;
    }

    try
    {
        return std::stoi(match[1].str());
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::string> commitHashFromJson(const std::string_view json)
{
    static const std::regex pattern(R"re("sha"\s*:\s*"([0-9a-fA-F]{40})")re");
    std::match_results<std::string_view::const_iterator> match;
    if (!std::regex_search(json.begin(), json.end(), match, pattern))
    {
        return std::nullopt;
    }

    std::string hash = match[1].str();
    std::ranges::transform(hash, hash.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return hash;
}

bool isValidCommitHash(const std::string_view hash)
{
    return hash.size() == 40 && std::ranges::all_of(hash, [](const unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

std::string trim(const std::string_view value)
{
    const auto first = std::ranges::find_if(value, [](const unsigned char character) {
        return std::isspace(character) == 0;
    });
    const auto last = std::find_if(value.rbegin(), value.rend(), [](const unsigned char character) {
        return std::isspace(character) == 0;
    }).base();
    return first >= last ? std::string() : std::string(first, last);
}

std::string downloadUrlForCommit(std::string urlTemplate, const std::string_view commit)
{
    constexpr std::string_view token = "{commit}";
    const auto position = urlTemplate.find(token);
    if (position == std::string::npos)
    {
        return urlTemplate;
    }
    urlTemplate.replace(position, token.size(), commit);
    return urlTemplate;
}

bool isJarArchive(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    unsigned char signature[4]{};
    stream.read(reinterpret_cast<char*>(signature), sizeof(signature));
    if (stream.gcount() != static_cast<std::streamsize>(sizeof(signature)))
    {
        return false;
    }

    return signature[0] == 'P' && signature[1] == 'K'
        && ((signature[2] == 3 && signature[3] == 4)
            || (signature[2] == 5 && signature[3] == 6)
            || (signature[2] == 7 && signature[3] == 8));
}
}
