# Vendored win-x64 binaries

`libeigenexports.dll` and `libdpss.dll` are copied byte-for-byte from
`tins-lib` at commit `06f0f126544e5ac8cf3d1974988a29f957e3d844`
(`src/TINS.Core/runtimes/win-x64/native/`), copied on 2026-08-25.

No C++ source is currently tracked anywhere for either library:

- `libeigenexports` wraps Eigen (header-only) for `SingularValueDecomposition`
  and, transitively, `PrincipalComponentAnalysis`.
- `libdpss` computes discrete prolate spheroidal sequences for multitaper
  spectral/coherence analysis (`SpikeSpectrumAnalyzer`, `SpikeFieldCoherenceMT`).

Until that source is recovered or rewritten, these two libraries are
**win-x64-only**: they are not built by CI and are not included in the
`TINS.Native.linux-x64` / `TINS.Native.osx-x64` / `TINS.Native.osx-arm64`
packages. Consumers on those platforms will get `DllNotFoundException` from
the features listed above.

Follow-up: recover or reimplement the source and fold it into this repo's
CI (CMake + vcpkg's Eigen port) so it can be cross-compiled like FFTW/OpenBLAS.
