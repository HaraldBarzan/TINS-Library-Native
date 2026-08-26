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

    Confirmed against a real "vcpkg install --triplet x64-windows" build (2026-08-26): MSVC
    output has NO "lib" prefix and no "-3"/"f-3" suffix -- the actual files are "fftw3.dll",
    "fftw3f.dll", "openblas.dll" (unlike a MinGW/Unix build, MSVC doesn't apply the
    lib-prefix/soname convention). But NativeImportResolver (and smoke/Resolver.cs's copy of
    it) only appends ".dll" to the base name on Windows -- it does not strip a "lib" prefix
    there the way it does for the Linux/macOS branch -- so it looks for "libfftw3-3.dll" /
    "libopenblas.dll" specifically. The win- branch below renames the real vcpkg output to
    those expected names on copy, so the packaged asset matches what the resolver actually
    probes for without needing to touch tins-lib's resolver.

    The Linux/macOS filenames are still NOT verified against a real build (no toolchain
    exercised there yet) -- the glob patterns for those platforms remain a best-effort
    guess based on standard lib-prefixed shared-object naming conventions, which (unlike
    Windows) should already match without renaming since non-MSVC toolchains apply that
    convention themselves. Confirm on the first real Linux/macOS CI run and adjust if a
    pattern misses or over-matches.
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

$copied = @()

if ($Rid -match "^win-") {
    # Real MSVC vcpkg output has no "lib" prefix / "-3" suffix -- rename on copy to match
    # what NativeImportResolver actually probes for on Windows (see .NOTES above).
    $renameMap = @{
        "fftw3.dll"    = "libfftw3-3.dll"
        "fftw3f.dll"   = "libfftw3f-3.dll"
        "openblas.dll" = "libopenblas.dll"
    }
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
}
else {
    # Linux/macOS: not yet verified against a real build -- best-effort glob patterns
    # assuming the toolchain applies standard lib-prefixed shared-object naming itself
    # (unlike Windows, so no rename should be needed here). "*fftw3l*" is deliberately
    # excluded (long-double precision, unused by TINS.Core).
    $patterns = switch -Regex ($Rid) {
        "^osx-"  { @("libfftw3.*.dylib", "libfftw3f.*.dylib", "libfftw3.dylib", "libfftw3f.dylib", "libopenblas.*.dylib", "libopenblas.dylib") }
        default  { @("libfftw3.so*", "libfftw3f.so*", "libopenblas.so*") }  # linux-*
    }
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
}

if (-not $copied) {
    throw "No FFTW/OpenBLAS shared libraries matched under $($searchDirs -join ', ') for RID '$Rid'. " +
          "Inspect that directory's actual contents and fix the glob patterns in this script."
}

Write-Host "Staged $($copied.Count) file(s) into $OutputRoot`:"
$copied | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
