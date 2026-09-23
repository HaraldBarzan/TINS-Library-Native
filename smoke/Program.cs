using System;
using System.Runtime.InteropServices;
using Tins.Native.Smoke;

// One positional arg selects which package family's symbols to check, since the core
// (TINS.Native.<rid>, OpenBLAS) and FFTW (TINS.Native.FFTW.<rid>) packages are now built,
// packed, and restored separately -- a core-only smoke run must not expect libfftw3-3/
// libfftw3f-3 to be present. "all" (or no arg, for local convenience) checks whatever is
// actually resolvable, for a manual run where both packages happen to be referenced.
var mode = args.Length > 0 ? args[0].ToLowerInvariant() : "all";
var checkCore = mode is "core" or "all";
var checkFftw = mode is "fftw" or "all";

int failures = 0;

if (checkFftw)
{
    Check("libfftw3-3", handle =>
    {
        // fftw_version is an exported `extern const char fftw_version[]` -- resolving it is
        // enough to prove the library loaded and its symbol table is intact.
        NativeLibrary.GetExport(handle, "fftw_version");
    });

    Check("libfftw3f-3", handle =>
    {
        NativeLibrary.GetExport(handle, "fftwf_version");
    });
}

if (checkCore)
{
    Check("libopenblas", handle =>
    {
        var export = NativeLibrary.GetExport(handle, "openblas_get_num_threads");
        var fn = Marshal.GetDelegateForFunctionPointer<GetNumThreads>(export);
        var threads = fn();
        Console.WriteLine($"    openblas_get_num_threads() = {threads}");
    });

    Check("libtins_pocketfft", handle =>
    {
        var versionExport = NativeLibrary.GetExport(handle, "tins_pocketfft_version");
        var version = Marshal.GetDelegateForFunctionPointer<GetVersion>(versionExport);
        var versionPtr = version();
        Console.WriteLine($"    tins_pocketfft_version() = {Marshal.PtrToStringAnsi(versionPtr)}");

        // A real create/execute/destroy round trip through P/Invoke, not just symbol presence --
        // this exercises the actual marshaling path (opaque handle, size_t, enum-as-int) the
        // pocketfft-shim/include/tins_pocketfft.h C ABI promises, distinct from the pure-C++
        // functional tests already run against this binary during development.
        var create = Marshal.GetDelegateForFunctionPointer<R2CCreate>(NativeLibrary.GetExport(handle, "tins_pocketfft_r2c_f32_create"));
        var execute = Marshal.GetDelegateForFunctionPointer<R2CExecute>(NativeLibrary.GetExport(handle, "tins_pocketfft_r2c_f32_execute"));
        var destroy = Marshal.GetDelegateForFunctionPointer<R2CDestroy>(NativeLibrary.GetExport(handle, "tins_pocketfft_r2c_f32_destroy"));

        const int size = 8;
        var status = create(new UIntPtr(size), out var plan);
        if (status != 0) throw new InvalidOperationException($"tins_pocketfft_r2c_f32_create failed: {status}");

        var input = new float[size];
        for (var i = 0; i < size; i++) input[i] = 1.0f; // constant signal -> DC bin == size, rest ~0
        var output = new float[(size / 2 + 1) * 2];

        status = execute(plan, input, output);
        destroy(plan);
        if (status != 0) throw new InvalidOperationException($"tins_pocketfft_r2c_f32_execute failed: {status}");
        if (Math.Abs(output[0] - size) > 1e-3) throw new InvalidOperationException($"expected DC bin == {size}, got {output[0]}");

        Console.WriteLine($"    r2c round trip via P/Invoke: DC bin = {output[0]} (expected {size})");
    });
}

Console.WriteLine(failures == 0 ? "\nAll native libraries resolved successfully." : $"\n{failures} check(s) FAILED.");
return failures == 0 ? 0 : 1;

void Check(string baseName, Action<IntPtr> verify)
{
    Console.Write($"[{baseName}] ");
    try
    {
        var handle = Resolver.Load(baseName);
        verify(handle);
        Console.WriteLine("OK");
    }
    catch (Exception ex)
    {
        failures++;
        Console.WriteLine($"FAILED: {ex.GetType().Name}: {ex.Message}");
    }
}

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
delegate int GetNumThreads();

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
delegate IntPtr GetVersion();

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
delegate int R2CCreate(UIntPtr size, out IntPtr outPlan);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
delegate int R2CExecute(IntPtr plan, [In] float[] input, [Out] float[] output);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
delegate void R2CDestroy(IntPtr plan);
