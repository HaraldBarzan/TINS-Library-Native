# tins-lib-native

Native (C/C++) dependency builds for [TINS-Library](https://github.com/HaraldBarzan/TINS-Library)
(`TINS.Core`), split into their own repo since they change far less often than the managed library
itself.

Packages are split into two license-separated families, not just by RID — see "Why two package
families" below before assuming this is one native package per RID:

- **`TINS.Native.<rid>`** (`win-x64`, `linux-x64`, `osx-x64`, `osx-arm64`) — **BSD-3-Clause.**
  OpenBLAS plus the PocketFFT shim (`pocketfft-shim/`, BSD-3-Clause, implemented, not yet run
  through real CI). This is the default package every consumer adds alongside `TINS.Core`. A fifth
  package, `TINS.Native.Desktop`, is a meta-package with no native content of its own that depends on
  all four RID packages, for a consumer that wants every desktop RID at once (e.g. a cross-platform
  test project).
- **`TINS.Native.FFTW.<rid>`** — **GPL-2.0-or-later, strictly opt-in.** FFTW binaries only. A
  consumer adds this *in addition to* the core package only if they specifically need FFTW (e.g.
  via `TINS.Core`'s `FFTW<T>.Plan`) and knowingly accepts the GPL obligation that comes with it for
  their own application.

Each package contains only `runtimes/<rid>/native/*` content, no managed assembly.

## Why two package families

FFTW is GPL-2.0-or-later. OpenBLAS and PocketFFT are both BSD-3-Clause. Bundling all three into one
package (the original design) would mean every consumer's application inherits GPL obligations just
from adding "the native package," whether or not they actually use FFTW — a real, not hypothetical,
licensing trap once this ships on nuget.org. Splitting FFTW into its own package puts that decision
where it belongs: on the consumer, at the point they explicitly reference it. This is the same
pattern projects like FFmpeg use to ship GPL and LGPL builds as separate artifacts.

## Scope (current)

| Library | win-x64 | linux-x64 | osx-x64 | osx-arm64 | Package | License |
|---|---|---|---|---|---|---|
| OpenBLAS (`libopenblas`) | CI-built (vcpkg) | CI-built (vcpkg) | paused (see below) | CI-built (vcpkg) | `TINS.Native.<rid>` | BSD-3-Clause |
| PocketFFT | implemented, verified locally (not yet in CI) | untested | untested | untested | `TINS.Native.<rid>` | BSD-3-Clause |
| FFTW (`libfftw3-3`, `libfftw3f-3`) | CI-built (vcpkg) | CI-built (vcpkg) | paused (see below) | CI-built (vcpkg) | `TINS.Native.FFTW.<rid>` | GPL-2.0-or-later, opt-in |

FFTW and OpenBLAS are built from source per-platform in CI via [vcpkg](https://vcpkg.io) (see
`vcpkg.json`, `.github/workflows/ci.yml`) — both from the same manifest/vcpkg install per RID; the
license split happens at staging/packing time (`scripts/stage-native.ps1 -Component Core|Fftw`), not
in what vcpkg builds. `osx-x64` is temporarily commented out of the CI matrix — not dropped from the
codebase, `pack/TINS.Native.osx-x64/` and `pack/TINS.Native.FFTW.osx-x64/` are untouched — both
because GitHub Actions' free monthly minutes (macOS runners cost 10x wall-clock time against that
quota) were exhausted standing up the other three RIDs, and because most Macs running this today are
Apple Silicon (`osx-arm64`) rather than Intel. Uncomment the matrix entry in `ci.yml` to bring it back.

`TINS.Core` used to also depend on two custom native wrappers with no tracked source anywhere
(`libeigenexports` for SVD/PCA, `libdpss` for multitaper analysis); both were replaced with pure
managed code directly in `TINS.Core` (SVD/PCA now use the `OpenBLAS.SGESVD` call already covered by
this repo; DPSS is a managed tridiagonal-eigensolver implementation), so there was never a need to
build or vendor them here.

`win-arm64` and `linux-arm64` are follow-ups, not yet in the CI matrix — pending verification of
GitHub-hosted native ARM runner availability.

## Publishing

CI builds and smoke-tests every push/PR and uploads each RID's `.nupkg` as a workflow artifact. There
is no automated publish step yet — download the artifacts and drop them into a local feed (e.g.
`C:\nugetlocal`, matching `TINS-Library`'s own local dev workflow) by hand.

## Repo layout

- `vcpkg.json` / `vcpkg-configuration.json` — manifest-mode dependencies (`fftw3`, `openblas`,
  `pocketfft`) and a pinned `builtin-baseline` for reproducible builds. FFTW/OpenBLAS are still built
  together per RID; the license split happens downstream of vcpkg, not here. `pocketfft` is an
  ordinary, unmodified registry dependency, vendoring only the header the shim below builds against.
- `pack/TINS.Native.<rid>/` — one minimal native-asset-only `.csproj` per RID (OpenBLAS + the
  pocketfft shim, BSD-3-Clause).
- `pack/TINS.Native.FFTW.<rid>/` — one minimal native-asset-only `.csproj` per RID (FFTW only,
  GPL-2.0-or-later, opt-in).
- `pack/TINS.Native.Desktop/` — meta-package depending on all four core RID packages, no native
  content of its own. (No FFTW equivalent yet — see CLAUDE.md.)
- `pocketfft-shim/` — a bespoke CMake project (not a vcpkg overlay port) implementing a thin,
  P/Invoke-able C ABI around header-only PocketFFT. Implemented and locally verified on win-x64
  (export symbols, a standalone functional test harness, and a full .NET P/Invoke round trip); not
  yet run through real CI.
- `scripts/stage-native.ps1` — copies vcpkg's build output into a flat `runtimes/<rid>/native/`
  staging folder consumed by the pack step, split by `-Component Core|Fftw` into the two license
  families' separate staging roots.
- `scripts/build-pocketfft-shim.ps1` — configures/builds/installs the pocketfft shim and copies its
  output into the same core staging folder as OpenBLAS.
- `smoke/` — a small console app with its own corrected native-library resolver (fixes a
  double-`lib`-prefix bug present in `TINS-Library`'s `NativeImportResolver.cs` as of this writing;
  that fix should be ported back there once this repo's non-Windows packages are proven). CI restores
  the just-built local core and FFTW packages into this project *separately* (add, run, remove; then
  the other) and runs it to prove each package's own shared libraries actually load and resolve
  symbols in isolation, not just that files exist in the package.

## Status

`win-x64`, `linux-x64`, and `osx-arm64` have all been built, packed, and smoke-tested successfully in
real CI, with genuine (not placeholder) native binaries. `osx-x64` is paused (see Scope above).
`TINS-Library`'s `TINS.Core` still bundles its own win-x64 natives directly (unchanged) while this repo
is being built out. The two will be reconciled — `TINS.Core` dropping its bundled natives in favor of
an explicit `TINS.Native.<rid>` reference — once the remaining pieces (real `osx-x64` if/when
re-enabled, and the PocketFFT shim replacing FFTW as the default FFT backend) are verified too.
