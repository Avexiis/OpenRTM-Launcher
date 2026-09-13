cmake_minimum_required(VERSION 3.24)

project(OpenRTMLauncher VERSION 1.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENRTM_BUILD_TESTS "Build launcher tests" ON)

include(FetchContent)

set(openrtm_icon_source "${CMAKE_CURRENT_SOURCE_DIR}/assets/openrtm.png")
set(openrtm_windows_manifest "${CMAKE_CURRENT_SOURCE_DIR}/resources/openrtm.manifest")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${openrtm_icon_source}" "${openrtm_windows_manifest}")

find_program(OPENRTM_IMAGEMAGICK_EXECUTABLE NAMES magick convert)
if(NOT OPENRTM_IMAGEMAGICK_EXECUTABLE)
    message(FATAL_ERROR "ImageMagick is required to generate launcher icons from assets/openrtm.png")
endif()

set(openrtm_generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(openrtm_icon_rgba "${openrtm_generated_dir}/openrtm-64.rgba")
set(openrtm_icon_png "${openrtm_generated_dir}/openrtm-256.png")
set(openrtm_icon_ico "${openrtm_generated_dir}/openrtm.ico")
set(openrtm_icon_rgba_header "${openrtm_generated_dir}/openrtm_icon_rgba.hpp")
set(openrtm_icon_png_header "${openrtm_generated_dir}/openrtm_icon_png.hpp")
set(openrtm_windows_resource "${openrtm_generated_dir}/openrtm_windows_icon.rc")
file(MAKE_DIRECTORY "${openrtm_generated_dir}")

execute_process(
    COMMAND "${OPENRTM_IMAGEMAGICK_EXECUTABLE}" "${openrtm_icon_source}"
        -auto-orient -background none -gravity center -resize 64x64 -extent 64x64
        -alpha on -depth 8 "RGBA:${openrtm_icon_rgba}"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${OPENRTM_IMAGEMAGICK_EXECUTABLE}" "${openrtm_icon_source}"
        -auto-orient -background none -gravity center -resize 256x256 -extent 256x256
        -strip "PNG32:${openrtm_icon_png}"
    COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
    COMMAND "${OPENRTM_IMAGEMAGICK_EXECUTABLE}" "${openrtm_icon_png}"
        -define icon:auto-resize=256,128,64,48,32,16 "${openrtm_icon_ico}"
    COMMAND_ERROR_IS_FATAL ANY
)

function(openrtm_embed_binary input output symbol)
    file(READ "${input}" binary_hex HEX)
    file(SIZE "${input}" binary_size)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " binary_bytes "${binary_hex}")
    file(WRITE "${output}"
        "#pragma once\n\n"
        "inline constexpr unsigned char ${symbol}[] = {\n${binary_bytes}\n};\n"
        "inline constexpr unsigned int ${symbol}_len = ${binary_size};\n")
endfunction()

openrtm_embed_binary("${openrtm_icon_rgba}" "${openrtm_icon_rgba_header}"
    openrtm_icon_rgba)
openrtm_embed_binary("${openrtm_icon_png}" "${openrtm_icon_png_header}"
    openrtm_icon_png)
file(WRITE "${openrtm_windows_resource}"
    "101 ICON \"${openrtm_icon_ico}\"\n"
    "1 24 \"${openrtm_windows_manifest}\"\n")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLTK_OPTIONS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FORMS OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_GL OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(FLTK_OPTION_PRINT_SUPPORT OFF CACHE BOOL "" FORCE)
set(FLTK_USE_SYSTEM_LIBJPEG OFF CACHE BOOL "" FORCE)
set(FLTK_USE_SYSTEM_LIBPNG OFF CACHE BOOL "" FORCE)
set(FLTK_USE_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
    set(FLTK_BACKEND_WAYLAND OFF CACHE BOOL "" FORCE)
    set(FLTK_BACKEND_X11 ON CACHE BOOL "" FORCE)
    set(FLTK_GRAPHICS_CAIRO OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_PANGO OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_XCURSOR OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_XFIXES OFF CACHE BOOL "" FORCE)
    set(FLTK_USE_XINERAMA OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(fltk
    URL https://github.com/fltk/fltk/archive/refs/tags/release-1.4.5.tar.gz
    URL_HASH SHA256=7715e69ce081fa9ce6da48bb0dd3b07a4cf2cf937813814c04272f36fff593ea
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(fltk)

if(CMAKE_CXX_COMPILER_TARGET MATCHES "linux-gnu")
    set(openrtm_linux_sdk_include "${CMAKE_BINARY_DIR}/openrtm-linux-sdk/include")
    file(MAKE_DIRECTORY "${openrtm_linux_sdk_include}")
    foreach(header_directory X11 fontconfig)
        if(NOT EXISTS "${openrtm_linux_sdk_include}/${header_directory}")
            file(CREATE_LINK "/usr/include/${header_directory}"
                "${openrtm_linux_sdk_include}/${header_directory}" SYMBOLIC)
        endif()
    endforeach()

    # FindX11 reports /usr/include, which would override Zig's versioned libc headers.
    foreach(fltk_target fltk fltk_images)
        foreach(property INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
            get_target_property(include_directories ${fltk_target} ${property})
            if(include_directories)
                list(FILTER include_directories EXCLUDE REGEX "(^|:)/usr/include>?$")
                list(APPEND include_directories
                    "$<BUILD_INTERFACE:${openrtm_linux_sdk_include}>"
                    "$<BUILD_INTERFACE:/usr/include/freetype2>")
                set_property(TARGET ${fltk_target} PROPERTY ${property} "${include_directories}")
            endif()
        endforeach()
    endforeach()

    get_target_property(fltk_link_libraries fltk INTERFACE_LINK_LIBRARIES)
    list(REMOVE_ITEM fltk_link_libraries "/usr/lib/libdl.a" "/usr/lib/libSM.so"
        "/usr/lib/libICE.so")
    list(PREPEND fltk_link_libraries dl)
    set_property(TARGET fltk PROPERTY INTERFACE_LINK_LIBRARIES "${fltk_link_libraries}")
endif()

add_library(openrtm_core STATIC src/core.cpp)
target_include_directories(openrtm_core PUBLIC include)

add_executable(openrtm main.cpp src/gui.cpp src/launcher.cpp src/launcher_log.cpp
    src/http_client.cpp
    "${openrtm_icon_rgba_header}" "${openrtm_icon_png_header}")
target_include_directories(openrtm PRIVATE include src "${openrtm_generated_dir}")
target_link_libraries(openrtm PRIVATE openrtm_core fltk::fltk)
target_compile_definitions(openrtm PRIVATE OPENRTM_LAUNCHER_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(openrtm_core PRIVATE /W4 /permissive-)
    target_compile_options(openrtm PRIVATE /W4 /permissive-)
else()
    target_compile_options(openrtm_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(openrtm PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_sources(openrtm PRIVATE "${openrtm_windows_resource}"
        "${openrtm_icon_ico}" "${openrtm_windows_manifest}")
    target_compile_definitions(openrtm PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(openrtm PRIVATE winhttp shell32 user32)
    set_target_properties(openrtm PROPERTIES OUTPUT_NAME "OpenRTM_Launcher" WIN32_EXECUTABLE TRUE)
    if(MINGW)
        # Use the CRT shipped in Windows itself. This avoids UCRT API-set loader
        # dependencies while keeping GCC, libstdc++, and winpthreads static.
        target_link_options(openrtm PRIVATE -mcrtdll=msvcrt-os
            -static -static-libgcc -static-libstdc++)
    endif()
else()
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    FetchContent_Declare(mbedtls
        URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2
        URL_HASH SHA256=a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(mbedtls)

    set(MBEDTLS_INCLUDE_DIR "${mbedtls_SOURCE_DIR}/include" CACHE PATH "" FORCE)
    set(MBEDTLS_LIBRARY mbedtls CACHE STRING "" FORCE)
    set(MBEDX509_LIBRARY mbedx509 CACHE STRING "" FORCE)
    set(MBEDCRYPTO_LIBRARY mbedcrypto CACHE STRING "" FORCE)
    set(MBEDTLS_USE_STATIC_LIBS ON CACHE BOOL "" FORCE)

    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
    set(CURL_USE_MBEDTLS ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(CURL_USE_CMAKECONFIG OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(curl
        URL https://curl.se/download/curl-8.22.0.tar.xz
        URL_HASH SHA256=f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)

    target_compile_definitions(openrtm PRIVATE CURL_STATICLIB)
    target_link_libraries(openrtm PRIVATE CURL::libcurl)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_link_options(openrtm PRIVATE -static-libgcc -static-libstdc++)
    endif()
    if(CMAKE_CXX_COMPILER_TARGET MATCHES "linux-gnu")
        target_link_options(openrtm PRIVATE -Wl,--allow-shlib-undefined)
    endif()
    set_target_properties(openrtm PROPERTIES OUTPUT_NAME "openrtm")
endif()

install(TARGETS openrtm RUNTIME DESTINATION .)

if(OPENRTM_BUILD_TESTS)
    enable_testing()
    add_executable(openrtm_tests tests/core_tests.cpp)
    target_link_libraries(openrtm_tests PRIVATE openrtm_core)
    add_test(NAME openrtm_core_tests COMMAND openrtm_tests)
    add_test(NAME openrtm_help COMMAND openrtm --help)
endif()
