# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## Project Overview

`tins-lib-native` builds and packages the native (C/C++) dependencies behind
[TINS-Library](https://github.com/HaraldBarzan/TINS-Library) (`TINS.Core`, at `c:\_code\tins-lib`
locally) — FFTW and OpenBLAS. It exists as a separate repo because these libraries change far less
often than TINS.Core itself, and the goal is one NuGet package per RID (`TINS.Native.win-x64`,
`TINS.Native.linux-x64`, `TINS.Native.osx-x64`, `TINS.Native.osx-arm64`) that TINS.Core consumers add
explicitly for whichever platform(s) they ship. A fifth package, `TINS.Native.Desktop`, is a
meta-package with no native content of its own — it just depends on all four RID packages, for a
consumer (e.g. a cross-platform test project) that wants every desktop RID at once instead of adding
each one individually.

`TINS.Core` used to also depend on two custom native wrappers with no tracked source anywhere
(`libeigenexports` for SVD/PCA, `libdpss` for multitaper analysis) — this repo originally vendored
pre-built win-x64-only copies of both (see git history around commit `390d0a3` if you need it). Both
were since replaced with pure managed code directly in `tins-lib` (`SingularValueDecomposition`
rewritten onto the `OpenBLAS.SGESVD` this repo already builds; DPSS reimplemented via a managed
tridiagonal eigensolver), so **this repo now only needs to build FFTW + OpenBLAS** — full parity
across all RIDs, no vendored/Windows-only content, no gap to track.

**Read `README.md` for the user-facing scope table and package layout.** This file is about how to
work on the repo itself.

## Decisions already made — do not re-litigate

These came out of an explicit design conversation and are settled. If you find yourself about to
propose an alternative to one of these, stop and re-read this list instead:

1. **One NuGet package per RID**, not one per native library. `TINS.Native.<rid>` contains only
   `runtimes/<rid>/native/*` content, no managed assembly.
2. **Build scope is FFTW + OpenBLAS only**, built from source per-platform via vcpkg (manifest mode,
   `vcpkg.json`) — identical across all four RIDs. `libeigenexports`/`libdpss` are gone from the
   picture entirely (see Project Overview above); do not reintroduce a `vendor/` directory or a
   win-x64-only special case without a new, explicit reason.
3. **Publishing is local-only for now.** CI builds, smoke-tests, and uploads nupkgs as workflow
   artifacts; nothing is pushed to nuget.org or any other feed. Don't add a publish step without
   being asked.
4. **`tins-lib` itself is untouched by this repo's work so far**, aside from the SVD/DPSS-elimination
   changes noted above (those landed directly in `tins-lib`, unrelated to this repo's own packaging).
   `TINS.Core` still bundles its own win-x64 FFTW/OpenBLAS natives via
   `src/TINS.Core/TINS.Core.csproj`'s `runtimes/**/native/*` glob, unchanged. That migration (dropping
   the glob, deleting `src/TINS.Core/runtimes/`, updating `README.md`, adding `TINS.Native.win-x64`
   references to `tests/TINS.Tests.Unit` and `tests/TINS.Sketching`, and porting the resolver fix below
   back into `TINS.Core`'s actual `NativeImportResolver.cs`) is a deliberate, separate, later pass —
   only do it if explicitly asked, and only after this repo's packages are proven working across all
   RIDs in CI.
5. **`win-arm64` and `linux-arm64` are stretch/follow-up RIDs**, not in the CI matrix yet — GitHub-hosted
   native ARM runner availability needs verifying before adding them.
6. **OpenBLAS is built via a local overlay port (`vcpkg-overlays/openblas/`), not vcpkg's stock port.**
   This is load-bearing, not a style choice: upstream vcpkg's `openblas` port unconditionally passes
   `-DBUILD_WITHOUT_LAPACK=ON` (no feature flag to turn it off), which drops LAPACK — including
   `SGESVD` — entirely. `tins-lib`'s `SingularValueDecomposition` depends on `OpenBLAS.SGESVD` (see
   Project Overview), so a stock-port build is silently broken for that path even though it links and
   loads fine. Do not drop this overlay or "simplify" back to the registry port without re-solving the
   LAPACK problem another way. Full detail and verification numbers below.
7. **Versioning is MinVer-driven**, same mechanism as `tins-lib` (`Directory.Build.props`, tag prefix
   `v`, no fixed `<Version>` per project). Unlike `tins-lib` — which leaves MinVer's default identifier
   at its own default (`alpha.0`, since it has no tags either) — this repo pins
   `MinVerDefaultPreReleaseIdentifiers` to `alpha.1`, so untagged builds pack as `0.0.0-alpha.1.<height>`
   instead of MinVer's un-configured `0.0.0-alpha.0.<height>`. Once real release tags (`vX.Y.Z`) start
   getting pushed, this identifier stops applying (it only governs the untagged/pre-first-tag case).
8. **`TINS.Native.Desktop`'s `PackageReference`s to the four RID packages use floating versions**
   (`0.0.0-alpha.1.*`), not `$(Version)`. MinVer only sets `$(Version)` via a build target that runs
   *after* restore, but restore needs a concrete version immediately to resolve those references —
   `$(Version)` is still unset (defaults to `1.0.0`) at that point, which fails restore (`NU1102`,
   tried it). Floating versions resolve against whatever's already sitting in the feed instead, no
   MSBuild property evaluation needed. This ties the floating pattern to whatever
   `MinVerDefaultPreReleaseIdentifiers` currently is — if that changes (point 7), update the pattern in
   `pack/TINS.Native.Desktop/TINS.Native.Desktop.csproj` to match.

## Status

- Full repo skeleton exists: `vcpkg.json`/`vcpkg-configuration.json`, four `pack/TINS.Native.<rid>/`
  packaging projects, `scripts/stage-native.ps1`, `.github/workflows/ci.yml`, `smoke/` test project.
- **Verified locally, win-x64, with a real vcpkg source build (2026-08-26):** ran an actual
  `vcpkg install --triplet x64-windows` (not a stand-in) against MSVC Build Tools, staged the output,
  packed `TINS.Native.win-x64.0.0.0-alpha.1.2.nupkg` straight into `C:\nugetlocal`, confirmed the
  `runtimes/win-x64/native/` layout via `unzip -l`, then round-tripped it into `smoke/` via
  `PackageReference` — all three libraries loaded and resolved symbols (including a live
  `openblas_get_num_threads()` call). This supersedes the earlier stand-in-binaries verification
  (which used `tins-lib`'s existing win-x64 FFTW/OpenBLAS binaries in place of a real vcpkg build).
- **Environment notes from that run**, in case a future session hits the same gaps on a fresh machine:
  - MSVC wasn't installed at all initially; VS 2022 Build Tools (C++ workload) was installed via
    `winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "... --installPath
    D:\VSBuildTools --add Microsoft.VisualStudio.Workload.VCTools ..."` — redirected off the `C:` drive
    since it's a multi-GB install. `vcpkg` finds it automatically afterward regardless of install path.
  - The local `vcpkg` install (`/c/vcpkg`) needed a plain `git fetch` before it could even see the
    pinned `builtin-baseline` commit as a git object.
  - Once fetched, that pinned baseline (`00c5775...`) turned out to be **internally inconsistent**, not
    just stale: its `baseline.json` claimed `fftw3@3.3.11`/`openblas@0.3.33`, but the version-history
    files at that same commit only go up to `3.3.10`/`0.3.29`. Repinned to
    `117524bad2789b8aa6954324a1bee4bffb7d6d09` — the commit the locally-installed `vcpkg` binary itself
    ships with, confirmed self-consistent for both ports. If this drifts again, re-pin to whatever
    commit the `vcpkg` binary in use actually reports (`git -C <vcpkg root> rev-parse HEAD`), not an
    arbitrary fetched `master` tip.
  - `scripts/stage-native.ps1`'s multi-argument `Join-Path` calls only work under `pwsh`/PS7 (what CI
    uses); Windows PowerShell 5.1 only accepts one child path per call. Rewritten as chained 2-arg
    `Join-Path` calls so the script runs under either — no behavior change under `pwsh`.
- **Still not verified:** everything on Linux/macOS (no GitHub remote exists yet for this repo — CI has
  never run). The win-x64 leg is real and proven end-to-end now; Linux/macOS still rely on the
  best-effort glob patterns in `scripts/stage-native.ps1` and are the next thing to actually exercise.
  A GitHub remote for this repo is expected soon (user is creating one); once it's wired up, redo the
  Linux/macOS legs for real via CI and replace the placeholder packages below.
- **`TINS.Native.linux-x64`/`osx-x64`/`osx-arm64` in `C:\nugetlocal` right now are PLACEHOLDER-only**
  (2026-08-26), not real builds — this machine is Windows-only with no Docker/WSL2 and no Mac hardware,
  so genuine Linux/macOS compiles aren't possible here; real ones need the GitHub Actions runners in
  `.github/workflows/ci.yml` (pending the GitHub remote above). The placeholder files are plain text
  stand-ins with a `PLACEHOLDER -- not a real build` marker line, named to match
  `stage-native.ps1`'s *existing, still-unverified* Linux/macOS glob patterns
  (`libfftw3.so`/`libfftw3f.so`/`libopenblas.so` for linux-x64; `libfftw3.dylib`/`libfftw3f.dylib`/
  `libopenblas.dylib` for both osx RIDs) so the staging script's non-Windows branch actually ran for
  real (mechanically) rather than being hand-faked at the final artifact. **Do not treat these three
  nupkgs as functional** — they exist purely to prove the packaging/aggregation plumbing below, and
  should be overwritten by real CI-built packages once available.
- **`pack/TINS.Native.Desktop/` added and verified (2026-08-26):** a meta-package with no native content
  of its own that depends on all four RID packages. Referencing just `TINS.Native.Desktop` from
  `smoke/` pulled in all four transitively — confirmed via `smoke/obj/project.assets.json` that every
  `runtimes/<rid>/native/*` asset resolved correctly — and a plain `dotnet build` (no RID specified)
  copied *all four* RID subfolders into `smoke/bin/.../runtimes/<rid>/native/` (not just the host's
  own win-x64), which the host (win-x64) then still loaded correctly via .NET's own deps.json-aware
  native-library probing of that nested folder. This proves the aggregation/packaging plumbing works;
  it does not (and cannot, from this Windows machine) prove the Linux/macOS binaries themselves are
  valid — see the placeholder note above.

## Known open uncertainties (verify before trusting, don't assume)

- Whether vcpkg's `x64-osx`/`arm64-osx` default triplets are static (confirmed for `x64-linux`; osx
  defaults were inferred from the existence of `x64-osx-dynamic`/`arm64-osx-dynamic` community
  triplets, not directly confirmed by reading `triplets/x64-osx.cmake`, which 404s at the expected
  path — macOS's default/dynamic split may be structured differently than Linux's).
- Whether GitHub still hosts an Intel macOS runner image (`macos-13` in the current workflow) — Apple
  Silicon has been the default (`macos-latest`/`macos-14`+) for a while and Intel images may be on a
  deprecation path. Check before relying on this leg.
- Exact filenames vcpkg emits for FFTW/OpenBLAS on Linux/macOS are still unconfirmed. **Windows is now
  confirmed** (see below) and turned out to need a rename step, so don't assume Linux/macOS's
  `lib`-prefixed `.so`/`.dylib` glob patterns in `stage-native.ps1` are actually right either — verify
  on the first real Linux/macOS build instead of trusting the current patterns.

## Confirmed: real vcpkg output on win-x64 needs renaming (fixed in `stage-native.ps1`)

A real `vcpkg install --triplet x64-windows` produces **`fftw3.dll`, `fftw3f.dll`, `fftw3l.dll`,
`openblas.dll`** — no `lib` prefix, no `-3`/`f-3` suffix (MSVC doesn't apply the MinGW/Unix
lib-prefix/soname convention the way `tins-lib`'s currently-bundled binaries do). But
`NativeImportResolver` (and `smoke/Resolver.cs`'s copy of it) only appends `.dll` to the base name on
Windows — it does **not** strip a `lib` prefix there the way it does on the Linux/macOS branch — so it
specifically looks for `libfftw3-3.dll` / `libopenblas.dll`. `scripts/stage-native.ps1`'s win- branch
now renames the real vcpkg output to those expected names on copy (`fftw3.dll` → `libfftw3-3.dll`,
`fftw3f.dll` → `libfftw3f-3.dll`, `openblas.dll` → `libopenblas.dll`), so the packaged asset matches
what the resolver actually probes for, without touching `tins-lib`'s resolver. `fftw3l.dll` is still
skipped (long-double precision, unused). Linux/macOS have **not** been confirmed to need the same
treatment — their toolchains normally apply `lib`-prefixing themselves, but that's still an assumption
until proven on a real build.

## Confirmed: stock vcpkg OpenBLAS is missing LAPACK entirely (fixed via overlay port)

Prompted by a real question: our first vcpkg-built `openblas.dll` was ~1.78MB vs. the ~51MB binary
`tins-lib` currently bundles. Verified via `dumpbin /exports` on both (run through the PowerShell tool,
not Bash — MSYS mangles a bare `/exports` flag into a path):

- **Stock vcpkg build:** 1,741 exports, zero LAPACK routines (no `gesvd`, `getrf`, `syev`, `geqrf`,
  `potrf` — nothing). Root cause, in vcpkg's own `ports/openblas/portfile.cmake`: it passes
  `-DBUILD_WITHOUT_LAPACK=ON -DNOFORTRAN=ON` to CMake **unconditionally** — not gated by any vcpkg
  feature, so there is no `[lapack]` feature to opt into. `tins-lib`'s bundled 51MB binary (9,568
  exports, includes `SGESVD` etc.) was almost certainly built upstream with MinGW + real `gfortran`,
  a different toolchain path than vcpkg's MSVC-based port.
- **Fix:** `vcpkg-overlays/openblas/` is a full copy of the upstream port with one flag flipped —
  `-DBUILD_WITHOUT_LAPACK=OFF` (keeping `-DNOFORTRAN=ON`; OpenBLAS ships C-translated LAPACK sources
  for exactly this no-Fortran-compiler case, which is how official Windows OpenBLAS DLLs get full
  LAPACK without a real `gfortran` — confirmed this combination doesn't need one). Wired in via
  `vcpkg-configuration.json`'s `overlay-ports` array, which vcpkg picks up automatically in manifest
  mode — **no CI YAML changes were needed**, `vcpkg install` in `ci.yml` just works once the overlay
  exists. Bumped `port-version` to `1` in the overlay's `vcpkg.json` to mark it as a locally-patched
  variant of `0.3.29`.
- **Rebuilt and reverified (2026-08-26):** win-x64 `openblas.dll` is now 10.46MB, 6,497 exports,
  confirmed present: `sgesvd_`, `LAPACKE_sgesvd`, plus `dgesvd_`/`cgesvd_`/`zgesvd_` (all precisions).
  Repacked `TINS.Native.win-x64.0.0.0-alpha.1.2.nupkg` into `C:\nugetlocal` with the corrected binary,
  cleared the stale cached copy at `~/.nuget/packages/tins.native.win-x64/`, and reran the smoke test
  clean. Still smaller than `tins-lib`'s 51MB bundled binary — that remaining gap is `dynamic-arch`
  (multi-microarchitecture runtime dispatch), deliberately deferred (see below), not a LAPACK gap.
- **`dynamic-arch` was explicitly deferred, not forgotten.** vcpkg's `openblas` port's `dynamic-arch`
  feature declares `"supports": "!windows | mingw"` — it isn't even offered for a plain MSVC build, so
  Windows stays single-target (optimized for the build machine's CPU) regardless. Whether to chase
  multi-target support on Linux/macOS (where the feature is at least offered) is an open follow-up,
  not yet decided. Single-target carries a real portability risk (illegal-instruction fault on a CPU
  older/different than the build machine's) that hasn't been addressed — don't assume it's fine for a
  real release without revisiting this.
- **This same LAPACK gap has not yet been verified fixed for Linux/macOS** — the overlay port change
  applies to all platforms (nothing win-x64-specific in the portfile edit), but only win-x64 has
  actually been rebuilt and re-checked with `dumpbin`-equivalent tooling. Confirm `SGESVD` is present
  in the Linux/macOS builds too once real CI produces them — don't assume the fix transfers untested.

## The `NativeImportResolver` double-`lib`-prefix bug

`tins-lib`'s `src/TINS.Core/Native/NativeImportResolver.cs` has a latent bug: every real call site
passes an already-`lib`-prefixed base name (e.g. `"libopenblas"`), but the resolver's Linux/macOS
candidate generation prepends *another* `lib` (`$"lib{baseName}.so"` → `"liblibopenblas.so"`). It
doesn't currently break anything because later fallback candidates in the same list happen to be
correct — but it's fragile. **This repo's fix lives only in `smoke/Resolver.cs`**, a standalone copy
with the prefix-stripping fix applied (see the doc comment on `Resolver.Load` there for the exact
mechanism). `tins-lib`'s actual file has **not** been touched — porting the fix back is part of the
deferred migration pass in point 4 above, not something to do from this repo directly.

## Build & verify commands

```bash
# Pack one RID's package (native binaries must already be staged under
# artifacts/runtimes/<rid>/native/ -- CI's stage-native.ps1 does this after "vcpkg install";
# locally, without vcpkg, you can point NativeStagingDir at any folder with the right *.dll/*.so/*.dylib
# files for a dry-run pack, e.g. testing against tins-lib's existing win-x64 binaries)
dotnet pack pack/TINS.Native.win-x64/TINS.Native.win-x64.csproj -c Release -o artifacts/nupkg

# Inspect the resulting package layout
unzip -l artifacts/nupkg/TINS.Native.win-x64.*.nupkg

# End-to-end local smoke test (mirrors what CI's "Smoke test" step does)
dotnet nuget add source "C:\_code\tins-lib-native\artifacts\nupkg" --name local-native
dotnet add smoke/Tins.Native.Smoke.csproj package TINS.Native.win-x64 --version <version-from-nupkg-filename> --source local-native
dotnet run --project smoke/Tins.Native.Smoke.csproj -c Release
# Clean up afterward -- both of these are test-only, not meant to be committed or left registered:
dotnet nuget remove source local-native
# (then revert the PackageReference dotnet add package just added to smoke/Tins.Native.Smoke.csproj --
#  CI adds/removes it per-RID dynamically; the committed file should have an empty ItemGroup there)
```

`dotnet add package ... --source <path>` with a *named* source can fail restore with `NU1101` for
unrelated packages (it silently restricts the *entire* restore to that one source, not just the
package being added) — pass the local feed alongside the default sources by registering it with
`dotnet nuget add source` first and then omitting `--source` from `add package`, or use the full path
directly as shown above.

## Critical files

| File | Purpose |
|---|---|
| `vcpkg.json` / `vcpkg-configuration.json` | vcpkg manifest — FFTW + OpenBLAS dependencies, pinned baseline, overlay-ports registration |
| `vcpkg-overlays/openblas/` | Overlay port fixing the stock port's `BUILD_WITHOUT_LAPACK=ON` (drops `SGESVD`, which `tins-lib` depends on) — see dedicated section above |
| `scripts/stage-native.ps1` | Flattens vcpkg's per-triplet build output into `runtimes/<rid>/native/` for packing — win- branch verified against a real build (renames to resolver-expected names); Linux/macOS branch still unverified |
| `pack/TINS.Native.<rid>/*.csproj` | Native-asset-only packaging projects, one per RID |
| `pack/TINS.Native.Desktop/*.csproj` | Meta-package depending on all four RID packages, no native content of its own |
| `.github/workflows/ci.yml` | Matrix build: vcpkg install → stage → pack → smoke test → upload artifact |
| `smoke/` | Proves a packed nupkg actually loads and resolves native symbols, not just that files exist |
