#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Configures, builds, and installs the pocketfft shim (pocketfft-shim/) via its own plain
    CMakeLists.txt, then copies the resulting shared library into the same core (BSD-3-Clause)
    staging folder scripts/stage-native.ps1 -Component Core uses -- so it packs into
    TINS.Native.<rid> alongside OpenBLAS with no separate pack project needed.

.PARAMETER Rid
    The .NET RID being built for, e.g. "win-x64", "linux-x64". Only used to pick the right CMake
    generator arguments (Windows needs an explicit -A x64) and to name the scratch build directory.

.PARAMETER PocketfftIncludeDir
    Directory containing pocketfft_hdronly.h -- vcpkg's installed include dir for the given triplet
    (e.g. "<vcpkg_installed>/<triplet>/include") after "vcpkg install" has run pocketfft's port.

.PARAMETER OutputRoot
    Where to copy the built shared library. Defaults to the same
    "<repo root>/artifacts/runtimes/<rid>/native" folder stage-native.ps1 -Component Core stages
    OpenBLAS into, so one pack step picks up both.

.NOTES
    Deliberately not a vcpkg overlay port -- see pocketfft-shim/CMakeLists.txt's own top comment for
    why. The build/install directories are scratch and safe to delete between runs.
#>
param(
    [Parameter(Mandatory)] [string] $Rid,
    [Parameter(Mandatory)] [string] $PocketfftIncludeDir,
    [string] $OutputRoot = (Join-Path (Join-Path (Join-Path $PSScriptRoot "..") "artifacts") (Join-Path "runtimes" (Join-Path $Rid "native")))
)

$ErrorActionPreference = "Stop"

$sourceDir = Join-Path (Join-Path $PSScriptRoot "..") "pocketfft-shim"
$buildDir = Join-Path (Join-Path $PSScriptRoot "..") (Join-Path "build" "pocketfft-shim-$Rid")
$installDir = Join-Path (Join-Path $PSScriptRoot "..") (Join-Path "build" "pocketfft-shim-$Rid-install")

$configureArgs = @(
    "-S", $sourceDir,
    "-B", $buildDir,
    "-DPOCKETFFT_INCLUDE_DIR=$PocketfftIncludeDir"
)
# Windows picks up the Visual Studio generator by default (multi-config -- CMAKE_BUILD_TYPE is
# meaningless there, "Release" is instead selected via --build's --config below; passing it anyway
# produces a "Manually-specified variables were not used" warning on cmake's STDERR, which a
# terminating $ErrorActionPreference = "Stop" turns into a script failure despite cmake itself
# succeeding -- confirmed the hard way, not a hypothetical). Linux/macOS default to a single-config
# Makefile generator, which needs CMAKE_BUILD_TYPE at configure time since there's no per-build
# --config to select it later; they also already target the host's native architecture on the
# GitHub-hosted runners this runs on, so no -A equivalent is needed there.
if ($Rid -like "win-*") {
    $configureArgs += @("-A", "x64")
} else {
    $configureArgs += @("-DCMAKE_BUILD_TYPE=Release")
}

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed for pocketfft shim ($Rid)" }

& cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake build failed for pocketfft shim ($Rid)" }

& cmake --install $buildDir --config Release --prefix $installDir
if ($LASTEXITCODE -ne 0) { throw "cmake install failed for pocketfft shim ($Rid)" }

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

# Matches CMakeLists.txt's install() rule: Windows .dll under bin/, Unix .so/.dylib under lib/.
$searchDirs = @("bin", "lib") | ForEach-Object { Join-Path $installDir $_ } | Where-Object { Test-Path $_ }
if (-not $searchDirs) {
    throw "Neither bin/ nor lib/ found under $installDir after installing the pocketfft shim"
}

$copied = @()
foreach ($dir in $searchDirs) {
    Get-ChildItem -Path $dir -File | Where-Object { $_.BaseName -eq "libtins_pocketfft" } | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination (Join-Path $OutputRoot $_.Name) -Force
        $copied += $_.Name
    }
}

if (-not $copied) {
    throw "No libtins_pocketfft.* file found under $($searchDirs -join ', ') -- check CMakeLists.txt's OUTPUT_NAME/PREFIX settings."
}

Write-Host "Staged $($copied.Count) file(s) into $OutputRoot`:"
$copied | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
