using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Tins.Native.Smoke;

/// <summary>
/// Standalone copy of tins-lib's <c>TINS.Native.NativeImportResolver</c>, with the
/// Linux/macOS double-"lib"-prefix bug fixed (see plan notes / tins-lib issue): callers
/// pass an already-"lib"-prefixed base name matching the Windows binary convention (e.g.
/// "libopenblas"), so the prefix must be stripped once before Linux/macOS candidate
/// generation re-adds it, instead of concatenating "lib" onto an already-prefixed name.
///
/// This copy exists so the smoke test can prove a freshly-packed TINS.Native.&lt;rid&gt;
/// nupkg actually resolves symbols on every platform, independent of whether the fix has
/// been ported back into tins-lib's own NativeImportResolver.cs yet (that port-back is a
/// separate, later step -- see the plan's Context section).
/// </summary>
public static class Resolver
{
    public static IntPtr Load(string libraryBaseName)
    {
        var coreName = libraryBaseName.StartsWith("lib", StringComparison.Ordinal)
            ? libraryBaseName["lib".Length..]
            : libraryBaseName;

        var asm = Assembly.GetExecutingAssembly();

        foreach (var name in GetPlatformLibraryNames(libraryBaseName, coreName))
        {
            if (NativeLibrary.TryLoad(name, asm, searchPath: null, out var handle))
                return handle;
        }

        var baseDir = AppContext.BaseDirectory;
        foreach (var fileName in GetPlatformLibraryFileNames(libraryBaseName, coreName))
        {
            var exePath = Path.Combine(baseDir, fileName);
            if (File.Exists(exePath) && NativeLibrary.TryLoad(exePath, out var handle))
                return handle;
        }

        throw new DllNotFoundException(
            $"Could not load native library '{libraryBaseName}' from default probing or exe directory. " +
            $"Searched: {string.Join(", ", GetPlatformLibraryFileNames(libraryBaseName, coreName))}");
    }

    private static string[] GetPlatformLibraryNames(string windowsName, string coreName)
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return [windowsName, $"{windowsName}.dll"];
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            return [$"lib{coreName}.so", $"lib{coreName}", $"{coreName}.so", coreName];
        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            return [$"lib{coreName}.dylib", $"{coreName}.dylib", coreName];
        return [coreName];
    }

    private static string[] GetPlatformLibraryFileNames(string windowsName, string coreName)
    {
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
            return [$"{windowsName}.dll"];
        if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux))
            return [$"lib{coreName}.so", $"lib{coreName}.so.3", $"lib{coreName}.so.2", $"lib{coreName}.so.1", $"{coreName}.so"];
        if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX))
            return [$"lib{coreName}.dylib", $"{coreName}.dylib"];
        return [coreName];
    }
}
