cmake_minimum_required(VERSION 3.20)
project(dabtx LANGUAGES C CXX VERSION 0.1.0)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(DABTX_SANITIZE "Enable ASan/UBSan in Debug builds" OFF)
option(DABTX_BUILD_TESTS "Build unit tests" ON)

# -----------------------------------------------------------------------------
# MSYS2 UCRT64 hint: ensure pkg-config finds libhackrf/fdk-aac/fftw/portaudio
# when CMake is launched without the environment variables set.
# -----------------------------------------------------------------------------
# Prefer our locally built fdk-aac-dabplus (960-sample granule) over the
# MSYS2 system fdk-aac 2.0.3 (which rejects GRANULE_LENGTH=960).
set(DABTX_LOCAL_PC "${CMAKE_SOURCE_DIR}/third_party/install/lib/pkgconfig")
if(WIN32)
    if(DEFINED ENV{PKG_CONFIG_PATH})
        set(ENV{PKG_CONFIG_PATH} "${DABTX_LOCAL_PC};C:/msys64/ucrt64/lib/pkgconfig;$ENV{PKG_CONFIG_PATH}")
    else()
        set(ENV{PKG_CONFIG_PATH} "${DABTX_LOCAL_PC};C:/msys64/ucrt64/lib/pkgconfig")
    endif()
endif()
if(WIN32 AND EXISTS "C:/msys64/ucrt64/bin/pkg-config.exe")
    set(PKG_CONFIG_EXECUTABLE "C:/msys64/ucrt64/bin/pkg-config.exe" CACHE FILEPATH "")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(HACKRF   REQUIRED IMPORTED_TARGET libhackrf)
# The MSYS2 2026.01.2 libhackrf package currently ships a stale absolute
# libusb include directory (D:/M/msys64/...).  Keep the valid UCRT64 paths
# explicitly so a portable UCRT64 build is not tied to that build machine.
if(WIN32)
    set_property(TARGET PkgConfig::HACKRF PROPERTY INTERFACE_INCLUDE_DIRECTORIES
        "C:/msys64/ucrt64/include;C:/msys64/ucrt64/include/libhackrf;C:/msys64/ucrt64/include/libusb-1.0")
endif()
pkg_check_modules(FDKAAC   REQUIRED IMPORTED_TARGET fdk-aac)
pkg_check_modules(FFTW3F   REQUIRED IMPORTED_TARGET fftw3f)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(SNDFILE  REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(TWOLAME  REQUIRED IMPORTED_TARGET twolame)

# Warnings
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

if(DABTX_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(third_party/fec)
add_subdirectory(src)

if(DABTX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
