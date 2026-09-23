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
