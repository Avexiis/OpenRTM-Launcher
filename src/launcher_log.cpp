#include "launcher_log.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace openrtm
{
namespace
{
std::filesystem::path logPath;
std::mutex logMutex;
bool logEnabled = false;

std::string timestamp()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    std::ostringstream value;
    value << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return value.str();
}
}

void initializeLauncherLog(const std::filesystem::path& dataDirectory) noexcept
{
    try
    {
        std::error_code error;
        const bool created = std::filesystem::create_directories(dataDirectory, error);
        if (error)
        {
            return;
        }
#ifndef _WIN32
        if (created)
        {
            std::filesystem::permissions(dataDirectory, std::filesystem::perms::owner_all,
                std::filesystem::perm_options::replace, error);
        }
#else
        (void)created;
#endif

        const std::scoped_lock lock(logMutex);
        logPath = dataDirectory / "launcher_log.log";
        std::ofstream output(logPath, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            logPath.clear();
            logEnabled = false;
            return;
        }
        output << "OpenRTM Launcher " OPENRTM_LAUNCHER_VERSION " log\n";
        output << '[' << timestamp() << "] Launcher started\n";
        logEnabled = static_cast<bool>(output);
    }
    catch (...)
    {
        logEnabled = false;
    }
}

void writeLauncherLog(const std::string_view message) noexcept
{
    try
    {
        const std::scoped_lock lock(logMutex);
        if (!logEnabled)
        {
            return;
        }
        std::ofstream output(logPath, std::ios::binary | std::ios::app);
        if (output)
        {
            output << '[' << timestamp() << "] " << message << '\n';
        }
    }
    catch (...)
    {
    }
}

void disableLauncherLog() noexcept
{
    const std::scoped_lock lock(logMutex);
    logEnabled = false;
    logPath.clear();
}
}
