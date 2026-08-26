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

> **Current package readiness: `TINS.Native.win-x64`, `TINS.Native.linux-x64`, and
> `TINS.Native.osx-arm64` are real and working — built, packed, and smoke-tested in CI with genuine
> vcpkg source builds. `TINS.Native.osx-x64` is NOT ready — no real build has ever been produced for
> it (only a non-functional placeholder), and it's currently paused out of the CI matrix pending
> GitHub Actions quota. Any downstream project (e.g. `tins-lib`) documenting or consuming these
> packages should reflect this: 3 of 4 RIDs ready, osx-x64 not yet.** See Status below for why and
> what it would take to finish it.

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

- **GitHub remote is live: `https://github.com/HaraldBarzan/TINS-Library-Native` (private repo).**
  Pushed 2026-08-26; CI has run for real repeatedly since. This supersedes every earlier "no remote
  yet" / "placeholder packages" note that used to be here.
- **win-x64, linux-x64, and osx-arm64 are all CONFIRMED fully green in real CI** (2026-08-26): genuine
  `vcpkg install` from source (not stand-ins), staged, packed, and smoke-tested successfully, with real
  `TINS.Native.<rid>` nupkgs uploaded as workflow artifacts. This is proven end-to-end, not just
  "should work" — see the CI debugging log below for exactly what it took.
- **osx-x64 is commented out of the CI matrix** (`.github/workflows/ci.yml`), not dropped from the
  codebase — `pack/TINS.Native.osx-x64/` and the RID package itself are untouched, and CI still had it
  queued (blocked on GitHub Actions quota, see below) when it was disabled. Two reasons: (1) this
  repo's free Actions minutes were exhausted standing up the other three legs, and (2) most Macs
  actually running this today are Apple Silicon (osx-arm64), so x64 is lower priority. Uncomment the
  matrix entry (clearly marked in the YAML) once quota allows or x64 Mac support is actually needed —
  the underlying build/package/smoke-test steps need no changes, they're identical across RIDs.
- **GitHub Actions free quota is a real, binding constraint on this repo.** Personal GitHub Free plan:
  2,000 included minutes/month, but macOS runners consume quota at **10x** wall-clock time (Linux is
  1x, Windows is 2x) — two full CI iterations with all 4 legs was enough to exhaust it. The account's
  Actions spending limit is set to $0, so exceeding the quota does **not** risk real charges — GitHub
  just refuses to allocate a runner for the job (it sits in `status: "queued"`, `runner_name: ""`
  indefinitely, not an error, not a hang) until the monthly quota resets or the limit is raised. Check
  actual usage at github.com/settings/billing before assuming there's headroom to iterate on CI again.
  **Don't push anything that triggers a new CI run without confirming there's quota first** (ask the
  user, or check the billing page) — this bit the session hard once already.
- **`pack/TINS.Native.Desktop/` added and verified (2026-08-26):** a meta-package with no native content
  of its own that depends on all four RID packages. Referencing just `TINS.Native.Desktop` from
  `smoke/` pulled in all four transitively — confirmed via `smoke/obj/project.assets.json` that every
  `runtimes/<rid>/native/*` asset resolved correctly — and a plain `dotnet build` (no RID specified)
  copied *all four* RID subfolders into `smoke/bin/.../runtimes/<rid>/native/` (not just the host's
  own win-x64), which the host then still loaded correctly via .NET's own deps.json-aware
  native-library probing of that nested folder.

### The CI debugging log — every real bug found getting to green, in order

Getting from "scaffold that had never run" to 3/4 platforms green took ~10 distinct real bugs, each
only discoverable by actually running CI (not locally reproducible on this Windows dev machine for the
non-Windows ones). Skim this before assuming a future CI failure is new — check whether it's actually
one of these regressing, or covered by a fix that got scoped incorrectly:

1. **Pinned `vcpkg-configuration.json` baseline was internally inconsistent** (not just stale) —
   `baseline.json` claimed versions absent from the version-history files at that same commit. Repinned
   to a self-consistent commit.
2. **Windows vcpkg output has no `lib` prefix / `-3` suffix** (`fftw3.dll` not `libfftw3-3.dll`) — the
   resolver needs the latter. Fixed via rename-on-stage in `scripts/stage-native.ps1`.
3. **Stock vcpkg `openblas` port always builds without LAPACK** (`-DBUILD_WITHOUT_LAPACK=ON`,
   unconditional, no feature flag) — silently drops `SGESVD`, which `tins-lib`'s SVD/PCA depends on.
   Fixed via `vcpkg-overlays/openblas/` (see dedicated section below).
4. **`lukka/run-vcpkg`'s GHA binary cache needs `ACTIONS_CACHE_URL`/`ACTIONS_RUNTIME_TOKEN` exported**
   — they exist in the workflow context but aren't exposed to shell steps automatically. Added an
   `actions/github-script` step to export them before vcpkg bootstraps.
5. **fftw3's (and, once LAPACK was enabled, `lapack-netlib`'s) bundled `CMakeLists.txt` predate CMake
   3.5** — CMake 4.x (GitHub runners' default) hard-removed compatibility with that. Setting
   `CMAKE_POLICY_VERSION_MINIMUM` as a job env var does **not** work — vcpkg doesn't forward arbitrary
   env vars into the per-port `cmake.exe` it spawns (confirmed: identical failure recurred with the var
   set). Fixed by passing `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` as an actual CMake `-D` define inside
   both the `fftw3` and `openblas` overlay ports' own `vcpkg_cmake_configure()` calls. (An earlier
   attempt pinned an old CMake globally via `lukka/get-cmake` — reverted, because it broke vcpkg's
   *own* newer scripts, which need CMake features 3.30.1 doesn't have.)
6. **`lukka/run-vcpkg`'s default (unpinned) vcpkg commit is stale** (Feb 2025) — its
   `scripts/cmake/vcpkg_acquire_msys.cmake` references an msys2-runtime package build every mirror has
   since pruned (confirmed: 404 on all 6 mirrors). Pinned `vcpkgGitCommitId` to a current tagged
   release. Independent of `vcpkg-configuration.json`'s `builtin-baseline` (that only governs
   port-version resolution, not which vcpkg-tool/scripts commit gets checked out).
7. **`actions/checkout@v4` defaults to a shallow (depth-1) clone** — MinVer can't compute commit height
   from one commit, and silently drops the height component entirely (`...alpha.1.nupkg` instead of
   `...alpha.1.<height>.nupkg`). Added `fetch-depth: 0`.
8. **`dotnet add package --prerelease` (no explicit version) failed against a freshly-added local
   folder source** (`error: There are no versions available`) even though the nupkg was right there.
   Fixed by extracting the exact version from the just-built nupkg's filename and passing `--version`
   explicitly.
9. **`dotnet add package --source <registered-name>` doesn't resolve the name** — it's treated as a
   literal path instead, which doesn't exist (`NU1301`). The source registered via
   `dotnet nuget add source --name X` is already in the default source set for a subsequent restore
   without needing `--source` at all; removed it from the smoke test step. (This is the same underlying
   `dotnet add package --source` unreliability documented further down for `--source <path>`/`NU1101` —
   two different failure modes off the same command, both solved by not passing `--source`.)
10. **Windows checks out `.patch` files with CRLF absent a `.gitattributes`**, corrupting vcpkg overlay
    patch files (`error: corrupt patch at`) even though the git-committed blobs are LF-clean. Added
    `.gitattributes` forcing `eol=lf` repo-wide.
11. **macOS vcpkg output also lacks the `-3`/`f-3` suffix** (`libfftw3.dylib`, not `libfftw3-3.dylib`)
    — same underlying mismatch as #2, wrongly assumed to be Windows-only since non-MSVC toolchains do
    apply the `lib` prefix (just not the `-3` suffix). Unified `stage-native.ps1` onto one explicit
    rename-map approach for all three platform families instead of Windows-only-renames-the-rest-globs.
12. **OpenBLAS's generic SIMD abstraction layer hits a longstanding, widely-reported OpenBLAS/GCC bug
    on Linux** — `inlining failed in call to 'always_inline' ... target specific option mismatch`.
    Disabling `NO_AVX512` alone just shifted the identical failure to AVX2/FMA. win-x64 (MSVC) and
    osx-arm64 (Apple Clang) never hit this at all. Fixed by disabling `NO_AVX`/`NO_AVX2`/`NO_AVX512`
    together, **scoped to `VCPKG_TARGET_IS_LINUX` only** (not applied to Windows/macOS, which don't
    need it and would otherwise lose real AVX2/AVX-512 GEMM performance for no reason) — see the
    dedicated LAPACK/overlay section below for detail on why this trades throughput for portability on
    Linux specifically.

## Known open uncertainties (verify before trusting, don't assume)

- **`osx-x64` filenames/behavior specifically** are still unconfirmed — it's the one RID that has never
  actually run in CI (commented out of the matrix for quota reasons, see Status). `osx-arm64` is
  confirmed and both Apple platforms should behave identically (same Clang toolchain, same vcpkg
  triplet family), but that's an inference, not a direct observation, until `osx-x64` is re-enabled and
  actually run.
- Whether GitHub still hosts an Intel macOS runner image (`macos-13`, what the disabled `osx-x64` leg
  targets) long-term — Apple Silicon has been the default (`macos-latest`/`macos-14`+) for a while and
  Intel images may be on a deprecation path. Check before re-enabling that leg.
- `SGESVD`/LAPACK presence has only been directly verified (via `dumpbin /exports`-equivalent symbol
  inspection) on win-x64. `linux-x64` and `osx-arm64` use the exact same overlay port with the same
  flags and built/packed/smoke-tested successfully, so it's very likely fine, but nobody has run
  `nm`/`objdump`/`otool` against those binaries specifically to confirm the LAPACK symbols are actually
  present the way win-x64's were.

## Confirmed: real vcpkg output needs renaming on every platform (fixed in `stage-native.ps1`)

Real vcpkg output never matches what `NativeImportResolver` (and `smoke/Resolver.cs`'s copy of it)
actually looks for, on any platform — confirmed for win-x64 and osx-arm64 (linux-x64 presumed identical
to osx-arm64 by the same Unix convention, not yet independently re-confirmed after the unification):

- **Windows (MSVC):** `fftw3.dll`, `fftw3f.dll`, `fftw3l.dll`, `openblas.dll` — no `lib` prefix, no
  suffix at all (MSVC doesn't apply the MinGW/Unix lib-prefix/soname convention the way `tins-lib`'s
  currently-bundled binaries do).
- **macOS/Linux:** `libfftw3.dylib`/`libfftw3.so` (the unversioned dev symlink CMake installs alongside
  the real soname-versioned file) — `lib`-prefixed as expected, but still no `-3`/`f-3` suffix.

But the resolver's Windows branch only appends `.dll` to the base name (no `lib`-stripping the way its
Linux/macOS branch does), and its Linux/macOS branch reconstructs `lib{coreName}.<ext>` from a base
name that already includes the `-3` — so on every platform it specifically wants
`libfftw3-3.<dll|so|dylib>` / `libfftw3f-3.<ext>`. `libopenblas` needs no rename on any platform (its
soname convention already matches what the resolver wants as-is). `scripts/stage-native.ps1` uses one
unified explicit rename-map (per-platform source names → resolver-expected names) rather than
Windows-specific renaming plus Linux/macOS glob-copying, which is what the codebase looked like before
this was confirmed to be a universal issue, not a Windows-only one. `fftw3l`/long-double is never
staged (unused by `tins-lib`'s `FFTW.cs`).

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
- **`dynamic-arch` remains explicitly deferred, not forgotten** — user's own call (2026-08-26): "we'll
  dedicate more time to [multi-target CPU dispatch] if we really need it... right now I expect most
  consumers would be using Windows or Mac." vcpkg's `openblas` port's `dynamic-arch` feature declares
  `"supports": "!windows | mingw"` — it isn't even offered for a plain MSVC build, so Windows stays
  single-target regardless of this decision either way. Single-target carries a real portability risk
  (illegal-instruction fault on a CPU older/different than the build machine's) that hasn't been
  addressed for any platform — don't assume it's fine for a real release without revisiting this.
- **Confirmed fixed on all three platforms that have actually run** (win-x64, linux-x64, osx-arm64) —
  each built, packed, and smoke-tested successfully with the LAPACK-enabled overlay; win-x64's symbols
  were independently re-verified with `dumpbin`, the other two weren't (see the uncertainty above).
  Linux additionally needed `NO_AVX`/`NO_AVX2`/`NO_AVX512` (see the CI debugging log's item 12) — a
  real GCC-specific compiler bug in OpenBLAS's generic SIMD layer, unrelated to LAPACK itself, scoped
  to Linux only so Windows/macOS keep full AVX2/AVX-512 performance. Considered and rejected switching
  away from OpenBLAS (to BLIS+libFLAME) to route around that Linux bug: no vcpkg port for either exists
  (checked `/c/vcpkg/ports/` directly — only `lapack`/`lapack-reference`/`clapack`, none of which are
  BLIS/libFLAME), it would mean two libraries and two build systems instead of one, and the actual bug
  is narrowly confined to one non-critical OpenBLAS source file, not a fundamental OpenBLAS problem —
  disproportionate effort for what it'd fix. `osx-x64` is unverified (never run, see Status).

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
dotnet add smoke/Tins.Native.Smoke.csproj package TINS.Native.win-x64 --version <version-from-nupkg-filename>
dotnet run --project smoke/Tins.Native.Smoke.csproj -c Release
# Clean up afterward -- both of these are test-only, not meant to be committed or left registered:
dotnet nuget remove source local-native
# (then revert the PackageReference dotnet add package just added to smoke/Tins.Native.Smoke.csproj --
#  CI adds/removes it per-RID dynamically; the committed file should have an empty ItemGroup there)
```

**Don't pass `--source` to `dotnet add package` at all once the feed is registered above.** Two
distinct, confirmed-in-practice failure modes come from this one flag: passing a *path* restricts the
entire restore to only that source (`NU1101` for unrelated packages, not just the one being added);
passing a registered source *name* isn't resolved as that name at all — it's treated as a literal path,
which doesn't exist (`NU1101`/`NU1301` depending on exact context — hit both in this repo's own CI
debugging, see the debugging log above). Once a source is registered via `dotnet nuget add source`, it
is already part of the default source set for any subsequent restore in that scope — just omit
`--source` entirely.

## Critical files

| File | Purpose |
|---|---|
| `vcpkg.json` / `vcpkg-configuration.json` | vcpkg manifest — FFTW + OpenBLAS dependencies, pinned baseline, overlay-ports registration |
| `vcpkg-overlays/openblas/` | Overlay port: LAPACK enabled (`BUILD_WITHOUT_LAPACK=OFF`), `CMAKE_POLICY_VERSION_MINIMUM=3.5`, `NO_AVX*` scoped to Linux only — see dedicated section above |
| `vcpkg-overlays/fftw3/` | Overlay port: `CMAKE_POLICY_VERSION_MINIMUM=3.5` only (fftw3 itself needs no LAPACK/AVX changes) — pulled fresh from the same vcpkg commit CI pins, not the stale local one |
| `.gitattributes` | Forces `eol=lf` repo-wide — Windows checking out the overlay `.patch` files as CRLF corrupted them (CI debugging log item 10) |
| `scripts/stage-native.ps1` | Flattens vcpkg's per-triplet build output into `runtimes/<rid>/native/` for packing — unified rename-map for all platforms, confirmed against real builds on win-x64/linux-x64/osx-arm64 |
| `pack/TINS.Native.<rid>/*.csproj` | Native-asset-only packaging projects, one per RID |
| `pack/TINS.Native.Desktop/*.csproj` | Meta-package depending on all four RID packages, no native content of its own |
| `.github/workflows/ci.yml` | Matrix build (win-x64/linux-x64/osx-arm64 active, osx-x64 commented out): vcpkg install → stage → pack → smoke test → upload artifact |
| `smoke/` | Proves a packed nupkg actually loads and resolves native symbols, not just that files exist |
