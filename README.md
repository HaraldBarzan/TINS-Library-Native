# tins-lib-native

Native (C/C++) dependency builds for [TINS-Library](https://github.com/HaraldBarzan/TINS-Library)
(`TINS.Core`), split into their own repo since they change far less often than the managed library
itself. CI builds one NuGet package per RID — `TINS.Native.win-x64`, `TINS.Native.linux-x64`,
`TINS.Native.osx-x64`, `TINS.Native.osx-arm64` — each containing only `runtimes/<rid>/native/*`
content, no managed assembly. Consumers add exactly the RID package(s) they ship for, alongside
`TINS.Core`.

## Scope (current)

| Library | win-x64 | linux-x64 | osx-x64 | osx-arm64 |
|---|---|---|---|---|
| FFTW (`libfftw3-3`, `libfftw3f-3`) | CI-built (vcpkg) | CI-built (vcpkg) | CI-built (vcpkg) | CI-built (vcpkg) |
| OpenBLAS (`libopenblas`) | CI-built (vcpkg) | CI-built (vcpkg) | CI-built (vcpkg) | CI-built (vcpkg) |
| Eigen wrapper (`libeigenexports`) | vendored binary only | not available | not available | not available |
| DPSS (`libdpss`) | vendored binary only | not available | not available | not available |

FFTW and OpenBLAS are built from source per-platform in CI via [vcpkg](https://vcpkg.io) (see
`vcpkg.json`, `.github/workflows/ci.yml`). `libeigenexports`/`libdpss` are TINS's own custom C++
wrappers; no source is currently tracked anywhere for them (see `vendor/win-x64/NOTES.md`), so they're
copied in as pre-built win-x64 binaries with no CI build step. Until that source is recovered or
rewritten, `SingularValueDecomposition`/`PrincipalComponentAnalysis` and multitaper spectral/coherence
analysis (`SpikeSpectrumAnalyzer`, `SpikeFieldCoherenceMT`) in `TINS.Core` remain win-x64-only.

`win-arm64` and `linux-arm64` are follow-ups, not yet in the CI matrix — pending verification of
GitHub-hosted native ARM runner availability.

## Publishing

CI builds and smoke-tests every push/PR and uploads each RID's `.nupkg` as a workflow artifact. There
is no automated publish step yet — download the artifacts and drop them into a local feed (e.g.
`C:\nugetlocal`, matching `TINS-Library`'s own local dev workflow) by hand.

## Repo layout

- `vcpkg.json` / `vcpkg-configuration.json` — manifest-mode dependencies (`fftw3`, `openblas`) and a
  pinned `builtin-baseline` for reproducible builds.
- `vendor/win-x64/` — pre-built `libeigenexports.dll` / `libdpss.dll`, copied from `TINS-Library`
  (see `NOTES.md` there for provenance).
- `pack/TINS.Native.<rid>/` — one minimal native-asset-only `.csproj` per RID.
- `scripts/stage-native.ps1` — copies vcpkg's build output into a flat `runtimes/<rid>/native/`
  staging folder consumed by the pack step.
- `smoke/` — a small console app with its own corrected native-library resolver (fixes a
  double-`lib`-prefix bug present in `TINS-Library`'s `NativeImportResolver.cs` as of this writing;
  that fix should be ported back there once this repo's non-Windows packages are proven). CI restores
  the just-built local package into this project and runs it to prove the shared libraries actually
  load and resolve symbols, not just that files exist in the package.

## Status

`TINS-Library`'s `TINS.Core` still bundles its own win-x64 natives directly (unchanged) while this
repo is being built out. The two will be reconciled — `TINS.Core` dropping its bundled natives in
favor of an explicit `TINS.Native.<rid>` reference — once the packages here are verified working
end-to-end.
