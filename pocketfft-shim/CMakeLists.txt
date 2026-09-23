# Bespoke CMake project for the pocketfft shim -- deliberately NOT a vcpkg overlay port, unlike
# FFTW/OpenBLAS. Those are vendored third-party libraries with build systems we don't control and
# didn't want to fight further than necessary (see CLAUDE.md's CI debugging log for the CMake-policy-
# version and msys2-mirror scars that came from exactly that). This is our own first-party source
# file with a modern CMakeLists.txt from day one, so none of that baggage applies -- it only needs
# vcpkg to have already installed pocketfft_hdronly.h somewhere (POCKETFFT_INCLUDE_DIR points at
# that), consumed as a plain include path rather than through vcpkg's toolchain machinery.
cmake_minimum_required(VERSION 3.20)
project(tins_pocketfft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED POCKETFFT_INCLUDE_DIR)
    message(FATAL_ERROR "POCKETFFT_INCLUDE_DIR must be set to the directory containing "
        "pocketfft_hdronly.h (e.g. vcpkg's <installed>/<triplet>/include, after "
        "'vcpkg install pocketfft'). Pass -DPOCKETFFT_INCLUDE_DIR=<path>.")
endif()

add_library(tins_pocketfft SHARED src/tins_pocketfft.cpp)

target_include_directories(tins_pocketfft PRIVATE
    "${POCKETFFT_INCLUDE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Every native library in this repo is staged/resolved by a "lib"-prefixed base name on every
# platform (see scripts/stage-native.ps1's notes on FFTW/OpenBLAS's real vcpkg output needing a
# rename to match this). CMake's own default only adds "lib" on Unix, not Windows -- set explicitly
# here so this target needs no post-build rename step at all, unlike FFTW/OpenBLAS.
set_target_properties(tins_pocketfft PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "tins_pocketfft"
)

install(TARGETS tins_pocketfft
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)
