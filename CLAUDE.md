# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## Project Overview

`tins-lib-native` builds and packages the native (C/C++) dependencies behind
[TINS-Library](https://github.com/HaraldBarzan/TINS-Library) (`TINS.Core`, at `c:\_code\tins-lib`
locally) — FFTW, OpenBLAS, and (once implemented) PocketFFT. It exists as a separate repo because
these libraries change far less often than TINS.Core itself.

**Packages are split into two license-separated families**, not just one package per RID (see
Decision 9 below for the full rationale):
- `TINS.Native.<rid>` (`win-x64`, `linux-x64`, `osx-x64`, `osx-arm64`) — **BSD-3-Clause.** OpenBLAS
  plus the PocketFFT shim (`pocketfft-shim/`, implemented — see Decision 10). This is the default
  package a TINS.Core consumer adds for whichever platform(s) they ship. `TINS.Native.Desktop` is a
  meta-package with no native content of its own — it depends on all four core RID packages, for a
  consumer (e.g. a cross-platform test project) that wants every desktop RID at once.
- `TINS.Native.FFTW.<rid>` — **GPL-2.0-or-later, strictly opt-in.** FFTW binaries only. A consumer
  adds this *in addition to* the core package only if they specifically need FFTW and knowingly accept
  the GPL obligation for their own application. No `Desktop`-style meta-package for this family yet —
  see Decision 9.

> **Current package readiness: `TINS.Native.win-x64`, `TINS.Native.linux-x64`, and
> `TINS.Native.osx-arm64` are real and working — built, packed, and smoke-tested in CI with genuine
> vcpkg source builds. `TINS.Native.osx-x64` is NOT ready — no real build has ever been produced for
> it (only a non-functional placeholder), and it's currently paused out of the CI matrix pending
> GitHub Actions quota. Any downstream project (e.g. `tins-lib`) documenting or consuming these
> packages should reflect this: 3 of 4 RIDs ready, osx-x64 not yet.** See Status below for why and
> what it would take to finish it.
>
> **The FFTW/core package split (Decision 9) and the pocketfft shim (Decision 10) are MERGED to
> `main`** (2026-09-24, fast-forwarded from `new-license-pocketfft` — both were confirmed green in
> real CI first: split, run
> [35883211678](https://github.com/HaraldBarzan/TINS-Library-Native/actions/runs/35883211678); shim,
> run [35891221998](https://github.com/HaraldBarzan/TINS-Library-Native/actions/runs/35891221998)).
> win-x64, linux-x64, and osx-arm64 all staged, packed, and smoke-tested both families successfully,
> with isolated smoke passes proving the core package never drags in FFTW, and the pocketfft shim's
> P/Invoke r2c round trip passing on all three. **Beyond this repo, the `tins-lib` side is also
> real now, on its own unmerged `pocketfft` branch** — native provider classes,
> `FftProviderRegistry<T>` registration, `FFTW<T>` made optional — and cross-repo integration was
> verified on win-x64 (2026-09-24): `tins-lib`'s `pocketfft` branch restored against this repo's
> packages, its full test suite passed, and its FFTW/managed/native three-way benchmark showed the
> pocketfft shim producing correct results at FFTW-comparable accuracy. linux-x64/osx-arm64
> cross-repo integration is still unverified (each side's own CI covers them independently, just not
> together) — see Decision 9's `tins-lib`-side bullet for what's left there.

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

1. **One NuGet package per (RID, license family)**, not one per native library and not simply one per
   RID either — revised by Decision 9 below from the original "one per RID" rule once a real licensing
   reason (not a style preference) forced the split. `TINS.Native.<rid>` and `TINS.Native.FFTW.<rid>`
   each contain only `runtimes/<rid>/native/*` content, no managed assembly.
2. **Build scope is FFTW + OpenBLAS (+ PocketFFT, once `pocketfft-shim/` is implemented) only**, built
   from source per-platform via vcpkg (manifest mode, `vcpkg.json`) — identical across all four RIDs.
   `libeigenexports`/`libdpss` are gone from the picture entirely (see Project Overview above); do not
   reintroduce a `vendor/` directory or a win-x64-only special case without a new, explicit reason.
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
8. **`TINS.Native.Desktop`'s `PackageReference`s to the four RID packages pin an explicit floor**
   (`0.0.0-alpha.1.14` as of 2026-08-26), not `$(Version)` and not a floating version. `$(Version)`
   doesn't work: MinVer only sets it via a build target that runs *after* restore, but restore needs a
   concrete version immediately (`$(Version)` is still unset/defaults to `1.0.0` at that point, which
   fails restore with `NU1102`, tried it). This floor has been through three iterations, keep the
   reasoning in mind before changing it again:
   1. **Floating** (`0.0.0-alpha.1.*`, the original choice): has no publishable meaning in a nuspec, so
      `dotnet pack` collapsed it to whatever concrete sibling version resolved *at pack time* and baked
      that in as the minimum — every repack silently ratcheted the floor up, risking a hard restore
      failure once an older sibling version got pruned from a downstream feed the floor now exceeded.
   2. **Abstract, height-less** (`0.0.0-alpha.1`, no ratcheting): never moves, but a literal
      `0.0.0-alpha.1` build will never exist, so `NU1603` ("dependency version not found, nearest match
      used") fired on *every single restore*, everywhere, permanently — benign but noisy enough in
      practice (reported from a real downstream consumer, AxoSync) to be worth avoiding.
   3. **Pinned to the current real height** (`0.0.0-alpha.1.14`, settled on): the one height all four
      sibling packages actually share in the local feed. Still a minimum-inclusive bound (plain,
      non-bracketed version strings mean `>= X` in nuspec dependency syntax, not an exact pin), so any
      later height still satisfies it once repacked — but this also stops an older cached copy in
      NuGet's global package cache (e.g. `1.12`) from silently satisfying the old height-less floor
      instead of the feed's actual current version, which is what was actually causing the `NU1603`
      noise (a stale local cache, not a feed content problem). Trade-off, accepted deliberately: this
      floor needs bumping by hand (edit here, then repack) whenever the siblings move to a new height
      that should become the new baseline — a manual step again, but only when actually intended, not
      silently on every incidental repack like option 1. Update this floor (and
      `MinVerDefaultPreReleaseIdentifiers`, point 7) together if that identifier changes.
9. **FFTW lives in its own opt-in package family (`TINS.Native.FFTW.<rid>`), separate from the core
   `TINS.Native.<rid>` package (OpenBLAS + PocketFFT).** This is a licensing decision, not a
   packaging-style one — revisit only if the underlying license facts change:
   - FFTW is GPL-2.0-or-later. OpenBLAS and PocketFFT (`pocketfft-shim/`, BSD-3-Clause upstream) are
     both BSD-3-Clause. The original design (one bundled package per RID, `Directory.Build.props`
     declaring a blanket `MIT` that didn't even match either license) would have meant every consumer's
     application inherited GPL obligations just from adding "the native package" — a real problem once
     this ships on nuget.org, not a hypothetical one.
   - `Directory.Build.props`'s default `PackageLicenseExpression` is now `BSD-3-Clause` (matches the
     core packages); the four `pack/TINS.Native.FFTW.<rid>/*.csproj` override it to
     `GPL-2.0-or-later` individually. Don't move the default back to a blanket value that covers both
     families again.
   - **Both libraries are still built from one `vcpkg install` per RID** (`vcpkg.json` keeps `fftw3`
     and `openblas` as siblings) — the split happens at staging/packing time, not in what vcpkg builds.
     `scripts/stage-native.ps1` takes a `-Component Core|Fftw` parameter and stages into two separate
     output roots (`artifacts/runtimes/<rid>/native` vs. `artifacts/fftw-runtimes/<rid>/native`);
     `.github/workflows/ci.yml` runs both staging invocations, both pack steps, and two *isolated*
     smoke-test passes per RID leg (add core package → run `smoke -- core` → remove it; add FFTW
     package → run `smoke -- fftw` → remove it) specifically to prove the core package alone never
     drags in FFTW.
   - **`TINS.Native.FFTW.Desktop` now exists** (added 2026-09-23, once the split's first CI height —
     `0.0.0-alpha.1.22` — existed to pin a floor to, per Decision 8's own reasoning). Unlike the core
     `TINS.Native.Desktop`, it references only win-x64/linux-x64/osx-arm64 — `TINS.Native.FFTW.osx-x64`
     has never been built, not even as a placeholder, so including it would break restore entirely
     rather than degrade gracefully. `TINS.Native.Desktop`'s own floor was bumped to `1.22` for
     win-x64/linux-x64/osx-arm64 at the same time; its osx-x64 entry deliberately stayed at `1.14`
     (the old placeholder, never rebuilt at the new height — see that `.csproj`'s own comment).
   - **On the `tins-lib` side (separate, later session, not started):** `FFTW<T>` needs to become a
     truly optional native provider (`NativeLibrary.TryLoad`, silently unavailable if
     `TINS.Native.FFTW.<rid>` isn't referenced) rather than a hard dependency, and PocketFFT — managed
     first, then native once `pocketfft-shim/` is implemented — becomes the default FFT backend. Until
     that lands, `tins-lib`'s existing FFTW-based code still needs `TINS.Native.FFTW.<rid>` added
     explicitly alongside the core package to keep working.
   - **License text bundling is done** (2026-09-24, `licenses/` — see that folder's own `README.md`
     for provenance/pinned-version detail). Every RID nupkg now bundles the actual upstream license
     text alongside `PackageLicenseExpression`, not just the metadata: BSD-3-Clause requires
     reproducing the copyright notice in redistributions, GPL requires including a copy of the
     license with distributed binaries — both real compliance requirements once this ships
     publicly, not style preferences. A top-level `LICENSE` (this repo's own BSD-3-Clause) was
     added too; there wasn't one before. Watch for an extensionless-`PackagePath` NuGet quirk if
     adding more vendored license files: it nests into a duplicate subfolder
     (`licenses/Foo-LICENSE/Foo-LICENSE`) instead of packing as one file unless the filename has an
     extension (`.txt` works fine for a license file with no natural format).
10. **The pocketfft shim (`pocketfft-shim/`) is a bespoke CMake project, not a vcpkg overlay port**
    — the one build-architecture question the interface draft had left open. It's our own first-party
    source (`src/tins_pocketfft.cpp` against `include/tins_pocketfft.h`), so none of the reasons that
    forced FFTW/OpenBLAS into overlay ports (fighting a vendored `CMakeLists.txt` we don't control)
    apply; a plain, modern `CMakeLists.txt` avoids all of that from the start. It only needs vcpkg for
    `pocketfft_hdronly.h` itself (`pocketfft` added as an ordinary, unmodified vcpkg dependency in
    `vcpkg.json` — confirmed self-consistent against the pinned baseline the same way the earlier
    fftw3/openblas baseline bug was checked), consumed as a plain `-DPOCKETFFT_INCLUDE_DIR=<path>`
    include path rather than through vcpkg's toolchain machinery. `scripts/build-pocketfft-shim.ps1`
    configures/builds/installs it and copies the output straight into the same core staging folder
    `stage-native.ps1 -Component Core` uses, so `TINS.Native.<rid>`'s existing pack step picks it up
    with no separate pack project.
    - **A real bug was caught and fixed before this ever reached CI: MSVC exports NOTHING from a DLL
      by default.** Unlike a Unix shared object (where a non-static `extern "C"` symbol is exported
      automatically), `extern "C"` on Windows only controls name mangling/calling convention, not
      visibility — a first build linked and loaded fine but `dumpbin /exports` showed an empty table.
      Fixed with a `TINS_POCKETFFT_API` macro (`__declspec(dllexport)`/`dllimport`, gated on a
      `TINS_POCKETFFT_BUILDING` define set only by `tins_pocketfft.cpp` itself) applied to every
      declaration in `tins_pocketfft.h`. Re-verified after the fix: all 19 expected symbols present
      via `dumpbin /exports`. FFTW/OpenBLAS never hit this because their own upstream build systems
      already handle it — this is the first time this repo has had to.
    - **The r2c/c2r direction-flag mapping was derived from reading pocketfft's actual vendored
      source, not guessed, then independently confirmed by a functional test harness** (a scratch
      C++ program linked directly against the built DLL, not just a symbol-presence check): the exact
      pinned-commit `pocketfft_hdronly.h` was fetched directly
      (`https://raw.githubusercontent.com/mreineck/pocketfft/9efd4da52cf8d28d14531d14e43ad9d913807546/pocketfft_hdronly.h`,
      matching `vcpkg/ports/pocketfft/portfile.cmake`'s pinned `REF`) and its `general_r2c`/
      `general_c2r` bodies read directly to confirm `r2c(..., forward=true, ...)` and
      `c2r(..., forward=false, ...)` are exact structural inverses (both use pocketfft's own
      "native", unnegated half-complex packing) — not a copy-paste assumption from FFTW's convention.
      Confirmed with a real round trip: constant-signal r2c gives DC bin == N with all other bins
      ~0, and r2c→c2r round-trips to `N * original` (unnormalized, matching this shim's stated FFTW-
      compatible convention). Also verified: in-place c2c forward→backward round trip, and a strided
      c2c call that only touches its targeted matrix column (the exact `FourierTransform2D`-enabling
      use case Decision 4 in `pocketfft-shim/README.md` depends on).
    - **`POCKETFFT_NO_MULTITHREADING` is defined when compiling `tins_pocketfft.cpp`** — multi-
      threading is explicitly out of scope (per the shim's own README), and this avoids needing a
      `-pthread` link dependency on Linux for code that would never use it anyway.
    - **CONFIRMED green in real CI on all three active platforms** (2026-09-23, branch
      `new-license-pocketfft`, run
      [35891221998](https://github.com/HaraldBarzan/TINS-Library-Native/actions/runs/35891221998)):
      win-x64, linux-x64, and osx-arm64 all built the shim, staged it into the core package, and
      passed the smoke test's P/Invoke r2c round trip. Not just an inference from win-x64 anymore.
      A trivial follow-up run (35894950658) also fixed a stray `/*` inside a block comment in
      `tins_pocketfft.h` (Clang `-Wcomment` on osx-arm64 — harmless, didn't fail the build, but
      worth not leaving in) — clean on all three platforms with no annotations after that fix.

## Status

- **GitHub remote is live: `https://github.com/HaraldBarzan/TINS-Library-Native` (private repo).**
  Pushed 2026-08-26; CI has run for real repeatedly since. This supersedes every earlier "no remote
  yet" / "placeholder packages" note that used to be here.
- **The FFTW/core license split (Decision 9) is CONFIRMED green in real CI** (2026-09-23, branch
  `new-license-pocketfft`, run
  [35883211678](https://github.com/HaraldBarzan/TINS-Library-Native/actions/runs/35883211678)):
  win-x64, linux-x64, and osx-arm64 all staged/packed/smoke-tested both `TINS.Native.<rid>` and
  `TINS.Native.FFTW.<rid>` successfully, with isolated smoke passes proving the core package never
  drags in FFTW. All 6 artifacts (3 core + 3 FFTW) uploaded.
- **`new-license-pocketfft` merged into `main`** (2026-09-24, fast-forward, user's own call) once
  the deferral criterion was actually met: the PocketFFT shim implemented, CI-green on all three
  active platforms, AND cross-repo integration verified against `tins-lib`'s `pocketfft` branch on
  win-x64 (see the Project Overview callout above and the memory note on this migration for detail).
  `main` now has the full FFTW/core split plus a working pocketfft shim — not just infrastructure,
  a real, tested alternative to FFTW, though `tins-lib` itself still needs its own branch merged
  before any downstream consumer actually gets it by default.
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

# End-to-end local smoke test (mirrors what CI's "Smoke test (core)" step does -- the FFTW
# package smoke-tests the same way, just with TINS.Native.FFTW.win-x64 and "-- fftw")
dotnet nuget add source "C:\_code\tins-lib-native\artifacts\nupkg" --name local-native
dotnet add smoke/Tins.Native.Smoke.csproj package TINS.Native.win-x64 --version <version-from-nupkg-filename>
dotnet run --project smoke/Tins.Native.Smoke.csproj -c Release -- core
# Clean up afterward -- both of these are test-only, not meant to be committed or left registered:
dotnet nuget remove source local-native
# (then revert the PackageReference dotnet add package just added to smoke/Tins.Native.Smoke.csproj --
#  CI adds/removes it per-RID dynamically; the committed file should have an empty ItemGroup there)
```

Since Decision 9, `scripts/stage-native.ps1` needs `-Component Core` or `-Component Fftw` before
packing either family locally (`Core` is the default, so a bare invocation still stages OpenBLAS as
before -- only staging FFTW needs the explicit flag now).

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
| `vcpkg.json` / `vcpkg-configuration.json` | vcpkg manifest — FFTW + OpenBLAS dependencies, pinned baseline, overlay-ports registration. Both libraries still built together; the license split happens downstream (see Decision 9) |
| `vcpkg-overlays/openblas/` | Overlay port: LAPACK enabled (`BUILD_WITHOUT_LAPACK=OFF`), `CMAKE_POLICY_VERSION_MINIMUM=3.5`, `NO_AVX*` scoped to Linux only — see dedicated section above |
| `vcpkg-overlays/fftw3/` | Overlay port: `CMAKE_POLICY_VERSION_MINIMUM=3.5` only (fftw3 itself needs no LAPACK/AVX changes) — pulled fresh from the same vcpkg commit CI pins, not the stale local one |
| `.gitattributes` | Forces `eol=lf` repo-wide — Windows checking out the overlay `.patch` files as CRLF corrupted them (CI debugging log item 10) |
| `scripts/stage-native.ps1` | Flattens vcpkg's per-triplet build output into `runtimes/<rid>/native/` for packing, split by `-Component Core\|Fftw` into two separate staging roots (Decision 9) — unified rename-map per component/platform, confirmed against real builds on win-x64/linux-x64/osx-arm64 pre-split |
| `pack/TINS.Native.<rid>/*.csproj` | Native-asset-only packaging projects, one per RID — OpenBLAS + the pocketfft shim, BSD-3-Clause |
| `pack/TINS.Native.FFTW.<rid>/*.csproj` | Native-asset-only packaging projects, one per RID — FFTW only, GPL-2.0-or-later, opt-in (Decision 9) |
| `pack/TINS.Native.Desktop/*.csproj` | Meta-package depending on all four core RID packages, no native content of its own. No FFTW equivalent yet (Decision 9) |
| `LICENSE` | This repo's own BSD-3-Clause license (its original content only — scripts, packaging projects, `pocketfft-shim/` source). Added 2026-09-24; there wasn't one before |
| `licenses/` | Vendored upstream license text for FFTW/OpenBLAS/pocketfft, packed into the matching nupkg(s) alongside `PackageLicenseExpression` — a real redistribution-compliance requirement (BSD/GPL both require it), not decoration. See `licenses/README.md` for exact provenance/pinned versions and the extensionless-`PackagePath` NuGet quirk this ran into |
| `pocketfft-shim/CMakeLists.txt`, `include/tins_pocketfft.h`, `src/tins_pocketfft.cpp` | Implemented (Decision 10) — a bespoke CMake project (not a vcpkg overlay port) exposing header-only pocketfft as a P/Invoke-able C ABI. `TINS_POCKETFFT_API` export macro is load-bearing on Windows (MSVC exports nothing from a DLL without it) |
| `scripts/build-pocketfft-shim.ps1` | Configures/builds/installs the pocketfft shim and copies its output into the same core staging folder `stage-native.ps1 -Component Core` uses |
| `.github/workflows/ci.yml` | Matrix build (win-x64/linux-x64/osx-arm64 active, osx-x64 commented out): vcpkg install → stage (core + fftw) → build pocketfft shim (core) → pack (core + fftw) → smoke test (core, then fftw, isolated) → upload artifact (core + fftw). Confirmed green end-to-end including the shim step (2026-09-23, runs 35883211678 and 35891221998) |
| `smoke/` | Proves a packed nupkg actually loads and resolves native symbols, not just that files exist. `Program.cs` takes a `core`/`fftw`/`all` arg so a core-only run doesn't expect FFTW symbols to be present; the core check now also round-trips a real r2c transform through the pocketfft shim via P/Invoke, not just a symbol-presence probe |
