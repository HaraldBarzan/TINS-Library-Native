/*
 * tins_pocketfft.h -- proposed C ABI for a thin shim around the header-only pocketfft (BSD-3-Clause)
 * C++ implementation, consumed by TINS.Core (tins-lib) via P/Invoke.
 *
 * STATUS: DRAFT / PROPOSAL. This header defines the interface tins-lib and tins-lib-native need to
 * agree on before the shim is implemented. Nothing in tins-lib calls into this yet, and no build
 * wiring (vcpkg overlay / stage-native.ps1 / pack/*.csproj) has been added for it yet -- that is
 * intentionally deferred until this shape is confirmed.
 *
 * WHY A SHIM AT ALL: unlike FFTW (a C library with a stable extern "C" ABI) and OpenBLAS (Fortran-
 * calling-convention C exports), pocketfft_hdronly.h is a C++ template header with no exported
 * symbols of its own -- there is nothing to P/Invoke into directly. This header is what turns it
 * into something with a stable, versioned, P/Invoke-able surface. It is the first custom native
 * shim in this repo (FFTW/OpenBLAS are both consumed as unmodified upstream builds via vcpkg
 * overlay ports); the implementation lives in this same `pocketfft-shim/` directory once agreed.
 *
 * DESIGN RULES:
 *   - Every function is extern "C", cdecl, and MUST NOT let a C++ exception cross this boundary --
 *     the implementation catches everything internally and reports failure via the returned
 *     tins_pocketfft_status.
 *   - Buffers are raw pointers with NO alignment requirement on the caller's side. This is a
 *     deliberate contract, not an oversight: tins-lib's existing FFTW wrapper was found to silently
 *     corrupt memory for some sizes when executed against arbitrary (non-FFTW-aligned) managed
 *     arrays via its "new-array execute" path. pocketfft has no such SIMD-alignment requirement by
 *     design, and this shim must not reintroduce one -- if any internal scratch/alignment is ever
 *     needed, it must be owned by the plan object, never pushed onto the caller.
 *   - Complex buffers are interleaved {real, imaginary} pairs of the plan's element type, e.g. for a
 *     size-N complex transform accessed at unit stride: N * 2 floats/doubles, laid out
 *     [re0, im0, re1, im1, ...]. This matches TINS.Algebra.Complex<T>'s layout exactly -- as of this
 *     draft, Complex<T> carries an explicit [StructLayout(LayoutKind.Sequential)] (re then im, no
 *     padding), so this is now a guaranteed ABI contract on the C# side, not just an observed
 *     default -- so a pinned Span<Complex<T>> can be passed straight through with no repacking.
 *   - c2c execute MUST support input == output (true in-place transform), matching the contract
 *     TINS.Core's IComplexFft1D<T> already establishes for the managed PocketFFT<T> backend, so a
 *     native provider can be a drop-in alternative. This holds regardless of the stride the plan was
 *     created with (see below) -- IComplexFft1D<T>'s in-place guarantee was proven to not depend on
 *     stride being 1 (every implementation fully reads its input before writing its output; a
 *     uniform stride multiplier on the addressing doesn't change that argument). r2c execute does
 *     not need to support aliasing -- input and output differ in both element type and length
 *     anyway, and IRealFft1D<T> does not offer an in-place overload for the same reason.
 *   - c2c plans carry an input and an output element stride, fixed at creation time (0 or negative
 *     is invalid; 1 is the ordinary contiguous case). This is what lets a caller transform, say, one
 *     column of a row-major matrix in place using the matrix's row length as the stride, with no
 *     transpose -- IComplexFft1D<T>.InputStride/OutputStride on the C# side, already implemented by
 *     the managed PocketFFT<T> provider. Stride is baked into plan creation and NOT taken by
 *     execute(), deliberately mirroring how both real backends actually support strided access:
 *     FFTW's advanced/guru interface (fftw_plan_many_dft) bakes istride/ostride into the plan, and
 *     pocketfft's own multi-dimensional entry points (general_nd, its c2c/r2c/c2r free functions)
 *     take stride_in/stride_out as part of building the transform, not as a per-call argument. A
 *     plan built for one stride cannot serve a different one -- a caller needing another stride
 *     creates another plan, exactly as for a different size. r2c does not have stride support yet;
 *     nothing on the C# side needs it (IRealFft1D<T> has none either) -- add it symmetrically if a
 *     real 2D-transform consumer ever does, rather than speculatively now.
 *   - Plan objects hold no shared mutable global state (pocketfft has no planner/wisdom cache the
 *     way FFTW does), so creating/destroying/executing *different* plan instances concurrently from
 *     multiple threads needs no external synchronization. A single plan instance is not required to
 *     be safe for concurrent execute() calls from multiple threads at once (same convention as
 *     FFTW and as TINS.Core's own managed PocketFFT<T>, which are also single-instance-single-
 *     threaded-at-a-time).
 *   - Sizes/strides are fixed at plan-creation time and are not re-validated at execute() time -- the
 *     caller (TINS.Core's native provider) is responsible for ensuring buffers are large enough,
 *     exactly as the existing FFTW wrapper and the managed PocketFFT<T> already require of their own
 *     callers. Minimum buffer length for a strided c2c plan is `(size - 1) * stride + 1` elements
 *     (matching IComplexFft1D<T>.Transform's own documented minimum).
 *   - c2r (complex-to-real, the inverse of r2c) is included even though no native provider calls it
 *     yet: TINS.Core's existing FFTW wrapper already exposes FFTW<T>.Plan.C2R_1D (unused by any
 *     production consumer, but present and callable), pocketfft has an equivalent c2r free function
 *     of its own, and tins-lib has since grown a full managed IComplexToRealFft1D<T> implementation
 *     (PocketFFTComplexToReal1D<T>) on the strength of that same argument. Leaving native c2r out of
 *     this shim would make it a narrower surface than what a caller could already reach directly
 *     through FFTW today -- a real regression, not a hypothetical one, since TINS.Core is consumed
 *     outside this repo and "we don't use it internally" doesn't mean a downstream consumer won't.
 *   - c2r execute() MUST NOT modify `input`, matching IComplexToRealFft1D<T>.Transform's contract on
 *     the C# side exactly (checked by an explicit test there). This needs calling out because it is
 *     NOT the default behavior of the native primitive a c2r implementation would most naturally
 *     wrap: FFTW's own c2r transforms are documented to potentially overwrite their input as part of
 *     computing the result (unlike its c2c/r2c, which never touch theirs), and the existing, unused
 *     FFTW<T>.Plan.C2R_1D in tins-lib doesn't request FFTW_PRESERVE_INPUT, so it already has that
 *     behavior today. Same resolution as the alignment design rule above: a quirk of one specific
 *     backend's primitive is the shim's problem to absorb (copy `input` to private scratch first, if
 *     the underlying primitive would otherwise destroy it), not something pushed onto every caller of
 *     this ABI. `input` is `const` in the execute signatures below precisely to make that a compile-
 *     time promise, not just a comment.
 */

#ifndef TINS_POCKETFFT_H
#define TINS_POCKETFFT_H

#include <stddef.h> /* size_t */

/* Unlike a Unix shared object (where a non-static extern "C" symbol is exported by default), an
 * MSVC-built DLL exports NOTHING unless explicitly told to -- extern "C" alone controls name
 * mangling/linkage, not visibility. Confirmed the hard way: a first build of this shim linked and
 * loaded fine but `dumpbin /exports` showed an empty table, so every declaration below needs
 * TINS_POCKETFFT_API. tins_pocketfft.cpp defines TINS_POCKETFFT_BUILDING before including this
 * header so the library's own translation unit exports rather than imports; nothing else needs to
 * define it (a P/Invoke consumer never #includes this header at all, but a hypothetical native C++
 * consumer linking against the shim would want the dllimport side, which is why this isn't simply
 * hardcoded to dllexport). Non-Windows needs no equivalent -- default ELF/Mach-O visibility already
 * exports extern "C" symbols, matching how FFTW/OpenBLAS's own upstream builds behave with no
 * special handling. */
#if defined(_WIN32) || defined(__CYGWIN__)
	#ifdef TINS_POCKETFFT_BUILDING
		#define TINS_POCKETFFT_API __declspec(dllexport)
	#else
		#define TINS_POCKETFFT_API __declspec(dllimport)
	#endif
#else
	#define TINS_POCKETFFT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the shim's own version string (e.g. "1.0.0"), for TINS.Core to distinguish "library not
 * found" from "library found but predates a function this version of TINS.Core needs" without a
 * symbol-existence probe per function. Does not reflect the vendored pocketfft version. */
TINS_POCKETFFT_API const char* tins_pocketfft_version(void);

typedef enum
{
	TINS_POCKETFFT_OK                    = 0,
	TINS_POCKETFFT_ERROR_INVALID_SIZE    = 1, /* size < 1, or a stride < 1 */
	TINS_POCKETFFT_ERROR_INVALID_ARGUMENT = 2, /* a required pointer/handle was null */
	TINS_POCKETFFT_ERROR_ALLOCATION_FAILED = 3,
	TINS_POCKETFFT_ERROR_UNKNOWN          = 4, /* an exception was caught and could not be classified */
} tins_pocketfft_status;

/* Deliberately mirrors TINS.Native.DFTSign's underlying values (Forward = -1, Backward = 1) so the
 * C# enum can be passed straight through as an int with no translation. */
typedef enum
{
	TINS_POCKETFFT_FORWARD  = -1,
	TINS_POCKETFFT_BACKWARD = 1,
} tins_pocketfft_sign;

/* Opaque plan handles. One distinct type per (precision, transform kind) so a caller can never pass
 * e.g. a single-precision plan to a double-precision execute function, or a c2c plan where an r2c
 * plan is expected -- that class of mistake is a compile error in C, not a runtime crash. */
typedef struct tins_pocketfft_plan_c2c_f32 tins_pocketfft_plan_c2c_f32;
typedef struct tins_pocketfft_plan_c2c_f64 tins_pocketfft_plan_c2c_f64;
typedef struct tins_pocketfft_plan_r2c_f32 tins_pocketfft_plan_r2c_f32;
typedef struct tins_pocketfft_plan_r2c_f64 tins_pocketfft_plan_r2c_f64;
typedef struct tins_pocketfft_plan_c2r_f32 tins_pocketfft_plan_c2r_f32;
typedef struct tins_pocketfft_plan_c2r_f64 tins_pocketfft_plan_c2r_f64;

/* ---- complex-to-complex, single precision ---- */

/* Create a plan for a length-`size` complex FFT in the given direction, reading strided by
 * `input_stride` and writing strided by `output_stride` (1 = contiguous; see the stride bullet
 * above). On success, writes the new plan to *out_plan and returns TINS_POCKETFFT_OK; on failure,
 * *out_plan is set to NULL. */
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_c2c_f32_create(size_t size, tins_pocketfft_sign sign,
	size_t input_stride, size_t output_stride, tins_pocketfft_plan_c2c_f32** out_plan);

/* Destroy a plan created by tins_pocketfft_c2c_f32_create. Passing NULL is a no-op. */
TINS_POCKETFFT_API void tins_pocketfft_c2c_f32_destroy(tins_pocketfft_plan_c2c_f32* plan);

/* Execute the transform: reads `size` complex samples (interleaved re/im pairs, `input_stride`
 * elements apart) from `input` and writes `size` complex samples the same way to `output`, per the
 * strides the plan was created with. input and output may be the exact same pointer (in-place). */
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_c2c_f32_execute(tins_pocketfft_plan_c2c_f32* plan, const float* input, float* output);

/* ---- complex-to-complex, double precision ---- */

TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_c2c_f64_create(size_t size, tins_pocketfft_sign sign,
	size_t input_stride, size_t output_stride, tins_pocketfft_plan_c2c_f64** out_plan);
TINS_POCKETFFT_API void tins_pocketfft_c2c_f64_destroy(tins_pocketfft_plan_c2c_f64* plan);
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_c2c_f64_execute(tins_pocketfft_plan_c2c_f64* plan, const double* input, double* output);

/* ---- real-to-complex, single precision ---- */
/* No `sign` parameter: real-to-complex and its inverse (complex-to-real, c2r, below) are each their
 * own fixed direction -- neither takes a sign the way c2c does. No stride parameters either, for the
 * same "nothing needs it yet" reason as c2c's stride bullet above; r2c and c2r can grow stride
 * support symmetrically with each other if that ever changes. */

/* Create a plan for a length-`size` real-input FFT. The one-sided complex output has
 * (size / 2 + 1) elements -- 2*(size/2+1) floats, interleaved re/im -- matching TINS.Core's existing
 * FFTW-based R2C convention (IRealFft1D<T>.OutputSize). */
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_r2c_f32_create(size_t size, tins_pocketfft_plan_r2c_f32** out_plan);
TINS_POCKETFFT_API void tins_pocketfft_r2c_f32_destroy(tins_pocketfft_plan_r2c_f32* plan);

/* Reads `size` real samples from `input` and writes (size/2 + 1) complex samples to `output`.
 * input and output must not overlap. */
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_r2c_f32_execute(tins_pocketfft_plan_r2c_f32* plan, const float* input, float* output);

/* ---- real-to-complex, double precision ---- */

TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_r2c_f64_create(size_t size, tins_pocketfft_plan_r2c_f64** out_plan);
TINS_POCKETFFT_API void tins_pocketfft_r2c_f64_destroy(tins_pocketfft_plan_r2c_f64* plan);
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_r2c_f64_execute(tins_pocketfft_plan_r2c_f64* plan, const double* input, double* output);

/* ---- complex-to-real, single precision (the inverse of r2c) ---- */
/* No native provider calls this yet -- included so this shim doesn't expose a narrower surface than
 * FFTW<T>.Plan.C2R_1D already does today, and to give the existing managed
 * IComplexToRealFft1D<T>/PocketFFTComplexToReal1D<T> on the tins-lib side a native counterpart to
 * eventually register at higher priority (see the c2r design-rule bullets above). */

/* Create a plan whose real output has `size` elements, reading the matching one-sided complex
 * spectrum ((size / 2 + 1) elements) as input -- the exact mirror of r2c's shapes. */
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_c2r_f32_create(size_t size, tins_pocketfft_plan_c2r_f32** out_plan);
TINS_POCKETFFT_API void tins_pocketfft_c2r_f32_destroy(tins_pocketfft_plan_c2r_f32* plan);

/* Reads (size/2 + 1) complex samples from `input` and writes `size` real samples to `output`.
 * `input` is not modified (see the c2r design-rule bullet above -- this is an ABI-level guarantee,
 * not just a description of what the current implementation happens to do). input and output must
 * not overlap. */
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_c2r_f32_execute(tins_pocketfft_plan_c2r_f32* plan, const float* input, float* output);

/* ---- complex-to-real, double precision ---- */

TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_c2r_f64_create(size_t size, tins_pocketfft_plan_c2r_f64** out_plan);
TINS_POCKETFFT_API void tins_pocketfft_c2r_f64_destroy(tins_pocketfft_plan_c2r_f64* plan);
TINS_POCKETFFT_API tins_pocketfft_status tins_pocketfft_c2r_f64_execute(tins_pocketfft_plan_c2r_f64* plan, const double* input, double* output);

#ifdef __cplusplus
}
#endif

#endif /* TINS_POCKETFFT_H */
