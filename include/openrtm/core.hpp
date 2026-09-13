#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace openrtm
{
std::optional<int> javaMajorFromRelease(std::string_view contents);
std::optional<std::string> commitHashFromJson(std::string_view json);
bool isValidCommitHash(std::string_view hash);
std::string trim(std::string_view value);
std::string downloadUrlForCommit(std::string urlTemplate, std::string_view commit);
bool isJarArchive(const std::filesystem::path& path);
}
