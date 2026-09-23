# pocketfft shim — interface proposal

Status: **draft, not yet implemented or wired into the build.** [`include/tins_pocketfft.h`](include/tins_pocketfft.h)
is the proposed C ABI for a thin `extern "C"` shim around the header-only
[pocketfft](https://github.com/mreineck/pocketfft) (`cpp` branch, `pocketfft_hdronly.h`,
BSD-3-Clause), so `TINS.Core` (in `tins-lib`) can P/Invoke into it as an alternative to FFTW.

This is the first custom native shim in this repo — FFTW and OpenBLAS are both consumed as
unmodified upstream builds via `vcpkg-overlays/`, since both already ship a stable C-callable ABI.
`pocketfft_hdronly.h` is C++ templates with no exported symbols of its own, so there's nothing to
P/Invoke into until something like this exists.

## Why this shape

- **Per-(precision, kind) opaque plan types**, not one generic `void*`: a caller can't pass a
  `float` plan where a `double` one is expected, or a c2c plan to an r2c execute — that mistake
  becomes a C compile error instead of a runtime crash.
- **No alignment requirement on caller buffers.** While building the managed PocketFFT provider in
  `tins-lib`, driving an *existing* FFTW plan (created with no bound buffers) through its
  `Execute(Span, Span)` overload against a plain managed array was found to silently corrupt memory
  for some sizes — an alignment mismatch between what the plan expected and what a bare array
  guarantees. pocketfft has no such requirement by design; this shim must not reintroduce one.
- **Interleaved `{re, im}` pairs**, matching `TINS.Algebra.Complex<T>`'s layout. This is now a
  *guaranteed* contract, not just an observed default: `Complex<T>` carries an explicit
  `[StructLayout(LayoutKind.Sequential)]` (added specifically because this shim, and TINS.Core's
  own `MemoryMarshal.Cast<Complex<T>, T>` usages elsewhere, depend on it) — so a pinned
  `Span<Complex<T>>` passes straight through with no repacking on the C# side.
- **c2c execute must support `input == output`** (true in-place) — `IComplexFft1D<T>` on the C#
  side already guarantees this for the managed backend, so a native provider needs to match it to
  be a drop-in alternative. This holds regardless of stride (see below) — the managed backend's
  in-place safety proof (every level fully reads its input before writing output) was checked to
  not depend on stride being 1. r2c doesn't need it (input/output differ in type and length anyway).
- **c2c plans carry `input_stride`/`output_stride`, fixed at creation, not passed to execute.**
  Added after the interface first shipped, once `tins-lib` actually needed it: `IComplexFft1D<T>`
  gained `InputStride`/`OutputStride` so a caller can transform one column of a row-major matrix in
  place (stride = row length) with no transpose. Baking stride into plan creation rather than
  execute isn't a C# quirk — it's how both real backends actually work: FFTW's advanced/guru
  interface (`fftw_plan_many_dft`) bakes `istride`/`ostride` into the plan, and pocketfft's own
  multi-dimensional entry points (`general_nd`, its `c2c`/`r2c`/`c2r` free functions) take
  `stride_in`/`stride_out` as part of building the transform, not per call. A plan built for one
  stride can't serve another — same as a plan built for one size can't serve another. r2c has no
  stride parameters yet, matching `IRealFft1D<T>` having none either; add symmetrically if a real
  consumer needs it, not speculatively.
- **`tins_pocketfft_sign` mirrors `TINS.Native.DFTSign`'s values exactly** (`Forward = -1`,
  `Backward = 1`) so the C# enum passes through as a plain `int`.
- **No C++ exception ever crosses the C boundary** — every function catches internally and reports
  `tins_pocketfft_status`.
- **c2r (complex-to-real) is in this draft, and now has a managed implementation on the `tins-lib`
  side too (`IComplexToRealFft1D<T>` / `PocketFFTComplexToReal1D<T>`).** Checked before deciding
  either way: `FFTW<T>.Plan.C2R_1D` already exists in `tins-lib`'s existing FFTW wrapper — unused by
  any production consumer, but present and callable today. Leaving c2r out of this shim would make
  the abstraction narrower than what's already reachable directly through FFTW, and `TINS.Core` is
  a library consumed outside this repo — "nothing inside tins-lib uses it" doesn't mean a downstream
  consumer won't reach for it, the same way they could already reach for `FFTW<T>.Plan.C2R_1D`
  directly. This is different from an internal implementation choice (radix-7 butterflies, a fused
  real-2D algorithm) where "no consumer yet" is still the right reason to wait — this is public
  surface area the layer underneath already promises.
- **c2r `execute()` must NOT modify `input`.** This one needed catching and fixing, not just
  deciding: an earlier draft of this document said the opposite (`execute()` "may overwrite its
  `input` buffer", matching FFTW's own default c2r behavior, since `FFTW<T>.Plan.C2R_1D` doesn't
  request `FFTW_PRESERVE_INPUT`). That directly contradicts `IComplexToRealFft1D<T>.Transform`'s
  contract on the C# side, which promises input survives (and has a test enforcing it). Same
  resolution as the alignment rule above: a quirk of one specific backend's primitive (FFTW's, not
  pocketfft's) is the shim's problem to absorb internally — copy `input` to private scratch first if
  the underlying primitive would otherwise destroy it — not something this ABI pushes onto callers.
  `tins_pocketfft_c2r_*_execute` takes a `const input` pointer now, making that a compile-time
  promise rather than a comment that's easy to drift out of sync with reality (as this one did).

## Open questions before implementation starts

1. **`good_size` exposure.** pocketfft internally has a notion of "good" (fast) transform sizes,
   similar to `Numerics.NextPow2` in `TINS.Core` but for pocketfft's own factor set. Worth exposing
   as `tins_pocketfft_good_size_c2c(size_t)` / `..._r2c(size_t)`? Deferred out of this draft;
   `TINS.Core`'s `Factorizer` already makes its own choices and doesn't need this to function, but
   it could help a caller pick a faster size deliberately (e.g. `Convolver`'s FFT-size selection).
2. **Bluestein/Rader threshold.** Should this be exposed as a tunable, or left as an internal
   implementation detail the shim decides on its own? Current lean: internal, matching how
   `PocketFFT<T>.BluesteinThreshold` in the managed implementation isn't exposed either.
3. **Versioning.** `tins_pocketfft_version()` returns the *shim's* version, not the vendored
   pocketfft commit/tag. Do we also want the latter exposed for diagnostics?
4. ~~2D / N-D entry point~~ **Resolved: 1D only, no fused N-D entry point of any kind, for now.**
   `FourierTransform2D` composes row/column 1D passes (and, since strides landed, can do a column
   pass in place instead of transposing) — that's sufficient. No `general_nd`-style fused entry
   point for c2c, r2c, or c2r. Revisit only if a real consumer needs the throughput a fused N-D
   transform would buy over composed 1D passes — not speculatively.

## Explicitly not in scope for this round

DCT/DST, multi-threading, and any fused N-D entry point (c2c, r2c, or c2r alike — see resolved
question 4 above) — none has a consumer on the `tins-lib` side, and 1D composition already covers
every real use today, so all three wait for an actual need rather than getting built ahead of one.

## Once the interface is agreed

- Vendor `pocketfft_hdronly.h` (as a vcpkg overlay port, or a plain fetched header if it doesn't
  need patching) and implement `tins_pocketfft.cpp` against it in this directory.
- Wire it into `scripts/stage-native.ps1` and each `pack/TINS.Native.<rid>/*.csproj`, alongside the
  existing FFTW/OpenBLAS outputs.
- On the `tins-lib` side: `NativePocketFftComplex1DProvider<T>` / `NativePocketFftReal1DProvider<T>`
  / `NativePocketFftComplexToReal1DProvider<T>`, implementing `IComplexFft1DProvider<T>` /
  `IRealFft1DProvider<T>` / `IComplexToRealFft1DProvider<T>` respectively, resolved the same way
  `FFTW.cs` resolves its own native exports (`NativeLibrary.GetExport` by name), registered into
  `FftProviderRegistry<T>` at a higher priority than the corresponding managed PocketFFT provider.
  All three C# interfaces already exist now (`IComplexToRealFft1D<T>`/`PocketFFTComplexToReal1D<T>`
  landed since this draft started) — this is now purely "write the native provider and register it",
  not "design an interface for it to plug into".
- Since pocketfft is BSD-3-Clause, its license text needs to actually be packed into the
  `TINS.Native.<rid>` nupkgs — flagged in passing during the native-repo survey that this currently
  isn't done for FFTW/OpenBLAS either. Worth fixing for all three at once rather than pocketfft only.
