#include "openrtm/core.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main()
{
    using namespace openrtm;

    int failures = 0;
    const auto expect = [&](const bool condition, const std::string_view description) {
        if (!condition)
        {
            std::cerr << "FAILED: " << description << '\n';
            ++failures;
        }
    };

    expect(javaMajorFromRelease("JAVA_VERSION=\"17.0.19\"\nIMPLEMENTOR=\"OpenJDK\"") == 17,
        "quoted Java 17 version");
    expect(javaMajorFromRelease("JAVA_VERSION=21\n") == 21, "unquoted Java version");
    expect(!javaMajorFromRelease("IMPLEMENTOR=\"OpenJDK\"").has_value(),
        "missing Java version");

    const std::string hash = "800936cff01a0a705b717e127389d80a069971a2";
    expect(commitHashFromJson("[{\"sha\":\"" + hash + "\"}]") == hash,
        "commit response parsing");
    expect(commitHashFromJson("{\"sha\":\"ABCDEFABCDEFABCDEFABCDEFABCDEFABCDEFABCD\"}")
            == "abcdefabcdefabcdefabcdefabcdefabcdefabcd",
        "commit normalization");
    expect(!commitHashFromJson("{\"message\":\"rate limited\"}").has_value(),
        "API error rejection");
    expect(isValidCommitHash(hash), "valid commit hash");
    expect(!isValidCommitHash("not-a-hash"), "invalid commit hash");

    expect(trim(" \r\n value\t") == "value", "whitespace trimming");
    expect(trim("  \n").empty(), "empty whitespace trimming");
    expect(downloadUrlForCommit("https://example.invalid/{commit}/OpenRTM.jar", hash)
            == "https://example.invalid/" + hash + "/OpenRTM.jar",
        "commit-pinned download URL");

    const auto temporary = std::filesystem::temp_directory_path() / "openrtm-core-test.jar";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write("PK\x03\x04", 4);
    }
    expect(isJarArchive(temporary), "JAR signature validation");
    std::error_code error;
    std::filesystem::remove(temporary, error);

    return failures == 0 ? 0 : 1;
}
