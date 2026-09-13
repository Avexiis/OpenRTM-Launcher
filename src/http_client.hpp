#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace openrtm
{
using ProgressCallback = std::function<void(std::uint64_t, std::optional<std::uint64_t>)>;

class HttpClient
{
public:
    std::string getText(const std::string& url, std::size_t maximumBytes = 1024 * 1024) const;
    void download(const std::string& url, const std::filesystem::path& destination,
        const ProgressCallback& progress) const;
};
}
