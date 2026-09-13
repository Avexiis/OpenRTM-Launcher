#!/usr/bin/env bash

set -euo pipefail

project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
linux_build="$project_dir/build/release-linux-glibc217"
windows_build="$project_dir/build/release-windows-gui"
dist_dir="$project_dir/dist"
tool_dir="$project_dir/build/tools"
zig_dir="$tool_dir/zig-x86_64-linux-0.16.0"
zig_archive="$tool_dir/zig-x86_64-linux-0.16.0.tar.xz"
zig_url="https://ziglang.org/download/0.16.0/zig-x86_64-linux-0.16.0.tar.xz"
zig_sha256="70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00"

if [[ ! -x "$zig_dir/zig" ]]; then
    cmake -E make_directory "$tool_dir"
    curl --fail --location --retry 3 "$zig_url" --output "$zig_archive"
    actual_sha256=$(sha256sum "$zig_archive" | cut -d ' ' -f 1)
    if [[ "$actual_sha256" != "$zig_sha256" ]]; then
        echo "Zig archive checksum verification failed" >&2
        exit 1
    fi
    cmake -E chdir "$tool_dir" cmake -E tar xf "$zig_archive"
fi

cmake -S "$project_dir" -B "$linux_build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/cmake/zig-linux-gnu-x86_64.cmake" \
    -DZIG_EXECUTABLE="$zig_dir/zig" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENRTM_BUILD_TESTS=ON
cmake --build "$linux_build"
ctest --test-dir "$linux_build" --output-on-failure

cmake -S "$project_dir" -B "$windows_build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/cmake/mingw-w64-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENRTM_BUILD_TESTS=OFF
cmake --build "$windows_build"

cmake -E make_directory "$dist_dir"
cmake -E copy "$linux_build/openrtm" "$dist_dir/openrtm"
cmake -E copy "$windows_build/OpenRTM_Launcher.exe" "$dist_dir/OpenRTM_Launcher.exe"
strip "$dist_dir/openrtm"
x86_64-w64-mingw32-strip "$dist_dir/OpenRTM_Launcher.exe"
chmod +x "$dist_dir/openrtm"

echo "Release artifacts:"
ls -lh "$dist_dir/openrtm" "$dist_dir/OpenRTM_Launcher.exe"
