#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Copies the shared libraries vcpkg just built for one triplet into a flat
    runtimes/<rid>/native/ staging folder, ready to be packed by the matching
    TINS.Native.<rid>.csproj.

.PARAMETER Triplet
    The vcpkg triplet that was just installed, e.g. "x64-windows", "x64-linux-dynamic".

.PARAMETER Rid
    The .NET RID this triplet corresponds to, e.g. "win-x64", "linux-x64".

.PARAMETER VcpkgInstalledRoot
    Path to vcpkg's "installed" output (the "vcpkg_installed" directory produced by
    manifest-mode installs), default assumes it sits next to this script's repo root.

.PARAMETER OutputRoot
    Where to stage the flattened runtimes/<rid>/native/ folder. Defaults to
    "<repo root>/artifacts/runtimes/<rid>/native".

.NOTES
    fftw3's vcpkg port builds three precision variants (fftw3, fftw3f, fftw3l) in one
    "vcpkg install fftw3" invocation. Only the double (fftw3) and single (fftw3f)
    precision libraries are needed here -- fftw3l (long double) is skipped, since
    TINS.Core's FFTW.cs only ever loads "libfftw3-3"/"libfftw3f-3".

    Confirmed against real vcpkg builds (2026-08-26, win-x64 and osx-arm64; linux-x64
    presumed identical to osx-arm64, same Unix shared-library convention, pending its own
    confirmation): NativeImportResolver (and smoke/Resolver.cs's copy of it) specifically
    looks for "libfftw3-3"/"libfftw3f-3"/"libopenblas" plus the platform's native
    extension -- but real vcpkg output never produces a file with that exact "-3"/"f-3"
    suffix on ANY platform:
      - Windows (MSVC): "fftw3.dll", "fftw3f.dll", "openblas.dll" -- no "lib" prefix, no
        suffix at all (MSVC doesn't apply the MinGW/Unix lib-prefix/soname convention the
        way tins-lib's currently-bundled binaries do).
      - macOS/Linux: "libfftw3.dylib"/"libfftw3.so" (the unversioned dev symlink CMake
        installs alongside the real soname-versioned file) -- "lib"-prefixed as expected,
        but still no "-3"/"f-3" suffix; OpenBLAS's soname convention needs no suffix at all
        ("libopenblas.so"/".dylib" is used as-is, which already matches what the resolver
        wants -- no rename needed for openblas specifically, on any platform).
    So every platform needs the fftw3/fftw3f rename below; only the source filenames
    differ. Renaming (rather than just glob-copying under the original name) is what makes
    the packaged asset match what the resolver actually probes for, without touching
    tins-lib's resolver itself. Only the single needed file per library is staged --
    versioned symlink variants (e.g. "libfftw3.3.6.9.dylib") are never referenced by the
    resolver and are deliberately not copied.
#>
param(
    [Parameter(Mandatory)] [string] $Triplet,
    [Parameter(Mandatory)] [string] $Rid,
    [string] $VcpkgInstalledRoot = (Join-Path (Join-Path $PSScriptRoot "..") "vcpkg_installed"),
    [string] $OutputRoot = (Join-Path (Join-Path (Join-Path (Join-Path $PSScriptRoot "..") "artifacts") "runtimes") (Join-Path $Rid "native"))
)

$ErrorActionPreference = "Stop"

$installedDir = Join-Path $VcpkgInstalledRoot $Triplet
if (-not (Test-Path $installedDir)) {
    throw "vcpkg installed directory not found: $installedDir (did 'vcpkg install --triplet $Triplet' run first?)"
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

# Shared libs land under bin/ on Windows (alongside .lib import libs in lib/), and under
# lib/ on Linux/macOS (no separate bin/ for shared objects there).
$searchDirs = @("bin", "lib") | ForEach-Object { Join-Path $installedDir $_ } | Where-Object { Test-Path $_ }
if (-not $searchDirs) {
    throw "Neither bin/ nor lib/ found under $installedDir"
}

# Real vcpkg source name -> name NativeImportResolver actually probes for, per platform.
$renameMap = switch -Regex ($Rid) {
    "^win-" {
        @{
            "fftw3.dll"    = "libfftw3-3.dll"
            "fftw3f.dll"   = "libfftw3f-3.dll"
            "openblas.dll" = "libopenblas.dll"
        }
    }
    "^osx-" {
        @{
            "libfftw3.dylib"    = "libfftw3-3.dylib"
            "libfftw3f.dylib"   = "libfftw3f-3.dylib"
            "libopenblas.dylib" = "libopenblas.dylib"
        }
    }
    default {
        # linux-*
        @{
            "libfftw3.so"    = "libfftw3-3.so"
            "libfftw3f.so"   = "libfftw3f-3.so"
            "libopenblas.so" = "libopenblas.so"
        }
    }
}

$copied = @()
foreach ($dir in $searchDirs) {
    foreach ($sourceName in $renameMap.Keys) {
        $sourcePath = Join-Path $dir $sourceName
        if (Test-Path $sourcePath) {
            $destName = $renameMap[$sourceName]
            Copy-Item -Path $sourcePath -Destination (Join-Path $OutputRoot $destName) -Force
            $copied += $destName
        }
    }
}

if (-not $copied) {
    throw "No FFTW/OpenBLAS shared libraries matched under $($searchDirs -join ', ') for RID '$Rid'. " +
          "Inspect that directory's actual contents and fix the rename map in this script."
}

Write-Host "Staged $($copied.Count) file(s) into $OutputRoot`:"
$copied | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
