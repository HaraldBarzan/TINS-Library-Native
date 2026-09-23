// tins_pocketfft.cpp -- implementation of the C ABI declared in include/tins_pocketfft.h, against
// the vendored header-only pocketfft (pocketfft_hdronly.h, BSD-3-Clause). See that header's own
// top-of-file comment for the full set of design rules this implementation must uphold (no
// exceptions crossing the boundary, no caller-buffer alignment requirement, in-place c2c, etc).
//
// r2c/c2r direction-flag note: pocketfft's own general_r2c()/general_c2r() were read directly (not
// guessed) to confirm r2c(..., forward=true, ...) and c2r(..., forward=false, ...) are exact
// structural inverses of each other -- both use the same "native", unnegated half-complex packing
// pocketfft's internal real-FFT kernel produces/consumes. r2c(forward=true) also matches the
// standard exp(-i...) DFT sign convention (FFTW_FORWARD), consistent with c2c's own forward/backward
// mapping below.
//
// POCKETFFT_NO_MULTITHREADING is defined for this translation unit: multi-threading is explicitly
// out of scope for this shim (see pocketfft-shim/README.md), and defining it means this file needs
// no pthread link dependency on Linux (pocketfft's <thread>/<mutex> usage is compiled out entirely).
#define POCKETFFT_NO_MULTITHREADING

// Must be defined before including tins_pocketfft.h so TINS_POCKETFFT_API resolves to
// __declspec(dllexport) rather than __declspec(dllimport) when this translation unit is compiled
// into the shim's own shared library (see the header's own comment on why this is needed at all --
// extern "C" alone does not export a symbol from an MSVC-built DLL).
#define TINS_POCKETFFT_BUILDING

#include "tins_pocketfft.h"
#include "pocketfft_hdronly.h"

#include <complex>
#include <cstddef>
#include <new>

using namespace pocketfft;

namespace
{
    constexpr const char* kShimVersion = "1.0.0";

    template <typename T>
    struct C2CState
    {
        std::size_t size;
        std::size_t input_stride;
        std::size_t output_stride;
        bool forward;
    };

    template <typename T>
    struct OneSidedState
    {
        std::size_t size;
    };
}

// Definitions of the opaque struct types declared (but left incomplete) in tins_pocketfft.h. Each
// is just a precision-tagged alias of the shared state structs above -- the distinct C types exist
// purely so the C API can't mix up plans of different (precision, kind) combinations at compile time.
struct tins_pocketfft_plan_c2c_f32 : C2CState<float> {};
struct tins_pocketfft_plan_c2c_f64 : C2CState<double> {};
struct tins_pocketfft_plan_r2c_f32 : OneSidedState<float> {};
struct tins_pocketfft_plan_r2c_f64 : OneSidedState<double> {};
struct tins_pocketfft_plan_c2r_f32 : OneSidedState<float> {};
struct tins_pocketfft_plan_c2r_f64 : OneSidedState<double> {};

namespace
{
    template <typename PlanT>
    tins_pocketfft_status create_c2c(std::size_t size, tins_pocketfft_sign sign,
        std::size_t input_stride, std::size_t output_stride, PlanT** out_plan)
    {
        if (out_plan == nullptr)
            return TINS_POCKETFFT_ERROR_INVALID_ARGUMENT;
        *out_plan = nullptr;
        if (size < 1 || input_stride < 1 || output_stride < 1)
            return TINS_POCKETFFT_ERROR_INVALID_SIZE;

        try
        {
            auto* plan = new PlanT();
            plan->size = size;
            plan->input_stride = input_stride;
            plan->output_stride = output_stride;
            plan->forward = (sign == TINS_POCKETFFT_FORWARD);
            *out_plan = plan;
            return TINS_POCKETFFT_OK;
        }
        catch (const std::bad_alloc&)
        {
            return TINS_POCKETFFT_ERROR_ALLOCATION_FAILED;
        }
        catch (...)
        {
            return TINS_POCKETFFT_ERROR_UNKNOWN;
        }
    }

    template <typename PlanT>
    tins_pocketfft_status create_one_sided(std::size_t size, PlanT** out_plan)
    {
        if (out_plan == nullptr)
            return TINS_POCKETFFT_ERROR_INVALID_ARGUMENT;
        *out_plan = nullptr;
        if (size < 1)
            return TINS_POCKETFFT_ERROR_INVALID_SIZE;

        try
        {
            auto* plan = new PlanT();
            plan->size = size;
            *out_plan = plan;
            return TINS_POCKETFFT_OK;
        }
        catch (const std::bad_alloc&)
        {
            return TINS_POCKETFFT_ERROR_ALLOCATION_FAILED;
        }
        catch (...)
        {
            return TINS_POCKETFFT_ERROR_UNKNOWN;
        }
    }

    template <typename PlanT>
    void destroy_plan(PlanT* plan)
    {
        delete plan;
    }

    template <typename T, typename PlanT>
    tins_pocketfft_status execute_c2c(PlanT* plan, const T* input, T* output)
    {
        if (plan == nullptr || input == nullptr || output == nullptr)
            return TINS_POCKETFFT_ERROR_INVALID_ARGUMENT;

        try
        {
            const shape_t shape{ plan->size };
            const shape_t axes{ 0 };
            const stride_t stride_in{ static_cast<ptrdiff_t>(plan->input_stride * sizeof(std::complex<T>)) };
            const stride_t stride_out{ static_cast<ptrdiff_t>(plan->output_stride * sizeof(std::complex<T>)) };

            const auto* in_cx = reinterpret_cast<const std::complex<T>*>(input);
            auto* out_cx = reinterpret_cast<std::complex<T>*>(output);

            // pocketfft itself requires stride_in == stride_out whenever data_in == data_out (see
            // its own sanity_check) -- the realistic in-place case this ABI targets (transforming a
            // matrix column/row in place) always has input_stride == output_stride, so this is not
            // a practical restriction; a caller deliberately aliasing buffers with mismatched
            // strides gets a clean error status here rather than a crash, via the catch-all below.
            c2c(shape, stride_in, stride_out, axes, plan->forward, in_cx, out_cx, static_cast<T>(1));
            return TINS_POCKETFFT_OK;
        }
        catch (const std::bad_alloc&)
        {
            return TINS_POCKETFFT_ERROR_ALLOCATION_FAILED;
        }
        catch (...)
        {
            return TINS_POCKETFFT_ERROR_UNKNOWN;
        }
    }

    template <typename T, typename PlanT>
    tins_pocketfft_status execute_r2c(PlanT* plan, const T* input, T* output)
    {
        if (plan == nullptr || input == nullptr || output == nullptr)
            return TINS_POCKETFFT_ERROR_INVALID_ARGUMENT;

        try
        {
            const shape_t shape_in{ plan->size };
            const stride_t stride_in{ static_cast<ptrdiff_t>(sizeof(T)) };
            const stride_t stride_out{ static_cast<ptrdiff_t>(sizeof(std::complex<T>)) };

            auto* out_cx = reinterpret_cast<std::complex<T>*>(output);

            r2c(shape_in, stride_in, stride_out, std::size_t(0), true, input, out_cx, static_cast<T>(1));
            return TINS_POCKETFFT_OK;
        }
        catch (const std::bad_alloc&)
        {
            return TINS_POCKETFFT_ERROR_ALLOCATION_FAILED;
        }
        catch (...)
        {
            return TINS_POCKETFFT_ERROR_UNKNOWN;
        }
    }

    template <typename T, typename PlanT>
    tins_pocketfft_status execute_c2r(PlanT* plan, const T* input, T* output)
    {
        if (plan == nullptr || input == nullptr || output == nullptr)
            return TINS_POCKETFFT_ERROR_INVALID_ARGUMENT;

        try
        {
            const shape_t shape_out{ plan->size };
            const stride_t stride_in{ static_cast<ptrdiff_t>(sizeof(std::complex<T>)) };
            const stride_t stride_out{ static_cast<ptrdiff_t>(sizeof(T)) };

            const auto* in_cx = reinterpret_cast<const std::complex<T>*>(input);

            c2r(shape_out, stride_in, stride_out, std::size_t(0), false, in_cx, output, static_cast<T>(1));
            return TINS_POCKETFFT_OK;
        }
        catch (const std::bad_alloc&)
        {
            return TINS_POCKETFFT_ERROR_ALLOCATION_FAILED;
        }
        catch (...)
        {
            return TINS_POCKETFFT_ERROR_UNKNOWN;
        }
    }
}

extern "C" const char* tins_pocketfft_version(void)
{
    return kShimVersion;
}

// ---- complex-to-complex, single precision ----

extern "C" tins_pocketfft_status tins_pocketfft_c2c_f32_create(std::size_t size, tins_pocketfft_sign sign,
    std::size_t input_stride, std::size_t output_stride, tins_pocketfft_plan_c2c_f32** out_plan)
{
    return create_c2c(size, sign, input_stride, output_stride, out_plan);
}

extern "C" void tins_pocketfft_c2c_f32_destroy(tins_pocketfft_plan_c2c_f32* plan)
{
    destroy_plan(plan);
}

extern "C" tins_pocketfft_status tins_pocketfft_c2c_f32_execute(tins_pocketfft_plan_c2c_f32* plan,
    const float* input, float* output)
{
    return execute_c2c<float>(plan, input, output);
}

// ---- complex-to-complex, double precision ----

extern "C" tins_pocketfft_status tins_pocketfft_c2c_f64_create(std::size_t size, tins_pocketfft_sign sign,
    std::size_t input_stride, std::size_t output_stride, tins_pocketfft_plan_c2c_f64** out_plan)
{
    return create_c2c(size, sign, input_stride, output_stride, out_plan);
}

extern "C" void tins_pocketfft_c2c_f64_destroy(tins_pocketfft_plan_c2c_f64* plan)
{
    destroy_plan(plan);
}

extern "C" tins_pocketfft_status tins_pocketfft_c2c_f64_execute(tins_pocketfft_plan_c2c_f64* plan,
    const double* input, double* output)
{
    return execute_c2c<double>(plan, input, output);
}

// ---- real-to-complex, single precision ----

extern "C" tins_pocketfft_status tins_pocketfft_r2c_f32_create(std::size_t size, tins_pocketfft_plan_r2c_f32** out_plan)
{
    return create_one_sided(size, out_plan);
}

extern "C" void tins_pocketfft_r2c_f32_destroy(tins_pocketfft_plan_r2c_f32* plan)
{
    destroy_plan(plan);
}

extern "C" tins_pocketfft_status tins_pocketfft_r2c_f32_execute(tins_pocketfft_plan_r2c_f32* plan,
    const float* input, float* output)
{
    return execute_r2c<float>(plan, input, output);
}

// ---- real-to-complex, double precision ----

extern "C" tins_pocketfft_status tins_pocketfft_r2c_f64_create(std::size_t size, tins_pocketfft_plan_r2c_f64** out_plan)
{
    return create_one_sided(size, out_plan);
}

extern "C" void tins_pocketfft_r2c_f64_destroy(tins_pocketfft_plan_r2c_f64* plan)
{
    destroy_plan(plan);
}

extern "C" tins_pocketfft_status tins_pocketfft_r2c_f64_execute(tins_pocketfft_plan_r2c_f64* plan,
    const double* input, double* output)
{
    return execute_r2c<double>(plan, input, output);
}

// ---- complex-to-real, single precision ----

extern "C" tins_pocketfft_status tins_pocketfft_c2r_f32_create(std::size_t size, tins_pocketfft_plan_c2r_f32** out_plan)
{
    return create_one_sided(size, out_plan);
}

extern "C" void tins_pocketfft_c2r_f32_destroy(tins_pocketfft_plan_c2r_f32* plan)
{
    destroy_plan(plan);
}

extern "C" tins_pocketfft_status tins_pocketfft_c2r_f32_execute(tins_pocketfft_plan_c2r_f32* plan,
    const float* input, float* output)
{
    return execute_c2r<float>(plan, input, output);
}

// ---- complex-to-real, double precision ----

extern "C" tins_pocketfft_status tins_pocketfft_c2r_f64_create(std::size_t size, tins_pocketfft_plan_c2r_f64** out_plan)
{
    return create_one_sided(size, out_plan);
}

extern "C" void tins_pocketfft_c2r_f64_destroy(tins_pocketfft_plan_c2r_f64* plan)
{
    destroy_plan(plan);
}

extern "C" tins_pocketfft_status tins_pocketfft_c2r_f64_execute(tins_pocketfft_plan_c2r_f64* plan,
    const double* input, double* output)
{
    return execute_c2r<double>(plan, input, output);
}
