#pragma once

#include <filesystem>
#include <string_view>

namespace openrtm
{
void initializeLauncherLog(const std::filesystem::path& dataDirectory) noexcept;
void writeLauncherLog(std::string_view message) noexcept;
void disableLauncherLog() noexcept;
}
