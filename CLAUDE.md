# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this
repository.

## Project Overview

`tins-lib-native` builds and packages the native (C/C++) dependencies behind
[TINS-Library](https://github.com/HaraldBarzan/TINS-Library) (`TINS.Core`, at `c:\_code\tins-lib`
locally) — FFTW, OpenBLAS, a custom Eigen wrapper (`libeigenexports`), and a DPSS/Slepian-sequence
library (`libdpss`). It exists as a separate repo because these libraries change far less often than
TINS.Core itself, and the goal is one NuGet package per RID (`TINS.Native.win-x64`,
`TINS.Native.linux-x64`, `TINS.Native.osx-x64`, `TINS.Native.osx-arm64`) that TINS.Core consumers add
explicitly for whichever platform(s) they ship.

**Read `README.md` for the user-facing scope table and package layout.** This file is about how to
work on the repo itself.

## Decisions already made — do not re-litigate

These came out of an explicit design conversation and are settled. If you find yourself about to
propose an alternative to one of these, stop and re-read this list instead:

1. **One NuGet package per RID**, not one per native library. `TINS.Native.<rid>` contains only
   `runtimes/<rid>/native/*` content, no managed assembly.
2. **This round's build scope is FFTW + OpenBLAS only**, built from source per-platform via vcpkg
   (manifest mode, `vcpkg.json`). `libeigenexports`/`libdpss` have **no tracked source anywhere** — not
   in this repo, not in `tins-lib`, not recovered from anywhere else. They are vendored as pre-built
   **win-x64-only** binaries in `vendor/win-x64/` (copied from `tins-lib` at commit
   `06f0f126544e5ac8cf3d1974988a29f957e3d844` — see `vendor/win-x64/NOTES.md`). Do not attempt to build
   them from source until that source is actually recovered or rewritten; that's an explicit follow-up,
   not something to solve opportunistically.
3. **Publishing is local-only for now.** CI builds, smoke-tests, and uploads nupkgs as workflow
   artifacts; nothing is pushed to nuget.org or any other feed. Don't add a publish step without
   being asked.
4. **`tins-lib` itself is untouched by this repo's work so far.** It still bundles its own win-x64
   natives via `src/TINS.Core/TINS.Core.csproj`'s `runtimes/**/native/*` glob, unchanged. That
   migration (dropping the glob, deleting `src/TINS.Core/runtimes/`, updating `README.md`, adding
   `TINS.Native.win-x64` references to `tests/TINS.Tests.Unit` and `tests/TINS.Sketching`, and porting
   the resolver fix below back into `TINS.Core`'s actual `NativeImportResolver.cs`) is a deliberate,
   separate, later pass — only do it if explicitly asked, and only after this repo's packages are
   proven working across all RIDs in CI.
5. **`win-arm64` and `linux-arm64` are stretch/follow-up RIDs**, not in the CI matrix yet — GitHub-hosted
   native ARM runner availability needs verifying before adding them.

## Status as of the initial scaffold (commit `584816e`)

- Full repo skeleton exists: `vcpkg.json`/`vcpkg-configuration.json`, four `pack/TINS.Native.<rid>/`
  packaging projects, `vendor/win-x64/` binaries, `scripts/stage-native.ps1`,
  `.github/workflows/ci.yml`, `smoke/` test project.
- **Verified locally, win-x64 only:** packed `TINS.Native.win-x64` (using `tins-lib`'s existing
  win-x64 FFTW/OpenBLAS binaries as a stand-in for what CI's vcpkg build will produce), confirmed the
  nupkg's `runtimes/win-x64/native/` layout is correct via `unzip -l`, then did a full round-trip —
  local folder feed → `PackageReference` → restore → run — and the smoke test successfully loaded all
  five libraries and called a live OpenBLAS export (`openblas_get_num_threads()`).
- **Not verified at all:** the actual vcpkg builds (FFTW/OpenBLAS built from source), and everything on
  Linux/macOS. **No GitHub remote exists yet for this repo — CI has never run.** The
  `scripts/stage-native.ps1` glob patterns for locating vcpkg's build output are a best-effort based on
  each library's documented naming convention, explicitly *not* verified against a real `vcpkg install`
  output (no vcpkg/C++ toolchain was available in the environment that scaffolded this repo). The
  first real CI run for each RID is the actual test — expect to have to adjust that script's patterns.
- The `vcpkg-configuration.json` `builtin-baseline` SHA was fetched from `microsoft/vcpkg`'s `master`
  branch HEAD at scaffold time; it will drift out of date and can be bumped freely, it's just a
  reproducibility pin.

## Known open uncertainties (verify before trusting, don't assume)

- Whether vcpkg's `x64-osx`/`arm64-osx` default triplets are static (confirmed for `x64-linux`; osx
  defaults were inferred from the existence of `x64-osx-dynamic`/`arm64-osx-dynamic` community
  triplets, not directly confirmed by reading `triplets/x64-osx.cmake`, which 404s at the expected
  path — macOS's default/dynamic split may be structured differently than Linux's).
- Whether GitHub still hosts an Intel macOS runner image (`macos-13` in the current workflow) — Apple
  Silicon has been the default (`macos-latest`/`macos-14`+) for a while and Intel images may be on a
  deprecation path. Check before relying on this leg.
- Exact filenames vcpkg emits per platform for FFTW/OpenBLAS shared libraries (particularly whether
  Windows output really matches today's bundled names — `libfftw3-3.dll`, `libopenblas.dll` — or
  something vcpkg-specific).

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
dotnet run --project smoke/Tins.Native.Smoke.csproj -c Release -p:SmokeRid=win-x64
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
| `vcpkg.json` / `vcpkg-configuration.json` | vcpkg manifest — FFTW + OpenBLAS dependencies, pinned baseline |
| `scripts/stage-native.ps1` | Flattens vcpkg's per-triplet build output into `runtimes/<rid>/native/` for packing — **the least-verified part of this repo** |
| `pack/TINS.Native.<rid>/*.csproj` | Native-asset-only packaging projects, one per RID |
| `vendor/win-x64/` | Pre-built `libeigenexports.dll`/`libdpss.dll`, no CI build |
| `.github/workflows/ci.yml` | Matrix build: vcpkg install → stage → pack → smoke test → upload artifact |
| `smoke/` | Proves a packed nupkg actually loads and resolves native symbols, not just that files exist |
