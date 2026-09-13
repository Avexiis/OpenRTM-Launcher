#pragma once

#include "http_client.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace openrtm
{
struct LauncherSnapshot
{
    std::filesystem::path dataDirectory;
    std::filesystem::path javaHome;
    int javaMajor = 0;
    bool javaAvailable = false;
    bool jarAvailable = false;
    bool desktopInstalled = false;
    std::uintmax_t jarBytes = 0;
    std::string localCommit;
    std::string remoteCommit;
    std::string updateError;
};

using StatusCallback = std::function<void(const std::string&)>;

LauncherSnapshot inspectLauncherStatus();
void prepareLatestForGui(bool forceUpdate, const StatusCallback& status,
    const ProgressCallback& progress);
void launchForGui();
void uninstallForGui();
void installDesktopForGui();
std::string jdkDownloadUrlForGui();
void openExternalUrl(const std::string& url);
}
