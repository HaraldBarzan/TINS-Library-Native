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

    The exact filenames vcpkg emits per platform have not been verified against a real
    build yet (no vcpkg/C++ toolchain available in this environment). The glob patterns
    below are a best-effort based on each library's documented upstream naming
    convention; the first real CI run for each RID should confirm the copied file list
    matches what NativeImportResolver actually probes for (see repo README), and this
    script should be adjusted if any pattern misses or over-matches.
#>
param(
    [Parameter(Mandatory)] [string] $Triplet,
    [Parameter(Mandatory)] [string] $Rid,
    [string] $VcpkgInstalledRoot = (Join-Path $PSScriptRoot ".." "vcpkg_installed"),
    [string] $OutputRoot = (Join-Path $PSScriptRoot ".." "artifacts" "runtimes" $Rid "native")
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

# Per-platform glob patterns for the libraries we ship. "*fftw3l*" is deliberately
# excluded (long-double precision, unused by TINS.Core).
$patterns = switch -Regex ($Rid) {
    "^win-"  { @("fftw3-3.dll", "fftw3f-3.dll", "libfftw3-3.dll", "libfftw3f-3.dll", "openblas.dll", "libopenblas.dll") }
    "^osx-"  { @("libfftw3.*.dylib", "libfftw3f.*.dylib", "libfftw3.dylib", "libfftw3f.dylib", "libopenblas.*.dylib", "libopenblas.dylib") }
    default  { @("libfftw3.so*", "libfftw3f.so*", "libopenblas.so*") }  # linux-*
}

$copied = @()
foreach ($dir in $searchDirs) {
    foreach ($pattern in $patterns) {
        Get-ChildItem -Path $dir -Filter $pattern -File -ErrorAction SilentlyContinue | ForEach-Object {
            if ($_.Name -notmatch "fftw3l") {
                Copy-Item -Path $_.FullName -Destination $OutputRoot -Force
                $copied += $_.Name
            }
        }
    }
}

if (-not $copied) {
    throw "No FFTW/OpenBLAS shared libraries matched under $($searchDirs -join ', ') for RID '$Rid'. " +
          "Inspect that directory's actual contents and fix the glob patterns in this script."
}

Write-Host "Staged $($copied.Count) file(s) into $OutputRoot`:"
$copied | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
