# Vendored third-party license texts

These are copies of the exact upstream license files for the native libraries this repo builds
and redistributes as compiled binaries, packed into the matching NuGet package(s) so the
package itself carries the copyright notice/license text its own upstream license requires for
redistribution (BSD-3-Clause requires reproducing the copyright notice; GPL requires including a
copy of the license with distributed binaries). `PackageLicenseExpression` in each `.csproj`
covers nuget.org's own license display; these files are the actual compliance artifact.

| File | Upstream project | License | Fetched from (exact pinned version/commit) | Packed into |
|---|---|---|---|---|
| `FFTW-COPYING.txt` | [FFTW](https://www.fftw.org/) 3.3.11 | GPL-2.0-or-later | `https://raw.githubusercontent.com/FFTW/fftw3/fftw-3.3.11/COPYING` (mirrors the same `fftw-3.3.11.tar.gz` release `vcpkg-overlays/fftw3/portfile.cmake` builds from) | `TINS.Native.FFTW.<rid>` only |
| `OpenBLAS-LICENSE.txt` | [OpenBLAS](https://github.com/OpenMathLib/OpenBLAS) v0.3.29 | BSD-3-Clause | `https://raw.githubusercontent.com/OpenMathLib/OpenBLAS/v0.3.29/LICENSE` | `TINS.Native.<rid>` |
| `PocketFFT-LICENSE.md` | [pocketfft](https://github.com/mreineck/pocketfft) commit `9efd4da52cf8d28d14531d14e43ad9d913807546` (`cpp` branch) | BSD-3-Clause | `https://raw.githubusercontent.com/mreineck/pocketfft/9efd4da52cf8d28d14531d14e43ad9d913807546/LICENSE.md` (same commit `vcpkg`'s `pocketfft` port and `pocketfft-shim/` build against) | `TINS.Native.<rid>` |

**Keep these in sync with the pinned versions.** If `vcpkg-overlays/fftw3/vcpkg.json`,
`vcpkg-overlays/openblas/vcpkg.json`, or `vcpkg.json`'s `pocketfft` dependency ever moves to a
different upstream version, re-fetch the matching license file from that exact version/commit and
replace it here -- don't assume the license text is unchanged across versions (copyright year
ranges and contributor lists do change).

`FFTW-COPYING.txt`/`OpenBLAS-LICENSE.txt` were renamed with a `.txt` extension from their
upstream extensionless `COPYING`/`LICENSE` names -- NuGet's pack step nests an extensionless
`PackagePath` into a duplicate subfolder (`licenses/OpenBLAS-LICENSE/OpenBLAS-LICENSE`) instead of
packing it as a single file; confirmed by actually packing and inspecting the nupkg contents, not
assumed.
