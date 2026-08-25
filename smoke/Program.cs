using System;
using System.Runtime.InteropServices;
using Tins.Native.Smoke;

int failures = 0;

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

Check("libopenblas", handle =>
{
    var export = NativeLibrary.GetExport(handle, "openblas_get_num_threads");
    var fn = Marshal.GetDelegateForFunctionPointer<GetNumThreads>(export);
    var threads = fn();
    Console.WriteLine($"    openblas_get_num_threads() = {threads}");
});

#if SMOKE_RID_win_x64
Check("libeigenexports", _ => { });
Check("libdpss", handle => NativeLibrary.GetExport(handle, "DPSS"));
#endif

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
