vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO OpenMathLib/OpenBLAS
    REF "v${VERSION}"
    SHA512 046316b4297460bffca09c890ecad17ea39d8b3db92ff445d03b547dd551663d37e40f38bce8ae11e2994374ff01e622b408da27aa8e40f4140185ee8f001a60
    HEAD_REF develop
    PATCHES
        disable-testing.diff
        getarch.diff
        system-check-msvc.diff
        win32-uwp.diff
)

vcpkg_check_features(OUT_FEATURE_OPTIONS OPTIONS
    FEATURES
        threads        USE_THREAD
        simplethread   USE_SIMPLE_THREADED_LEVEL3
        dynamic-arch   DYNAMIC_ARCH
)

# If not explicitly configured for a cross build, OpenBLAS wants to run 
# getarch executables in order to optimize for the target.
# Adapting this to vcpkg triplets:
# - install-getarch.diff introduces and uses GETARCH_BINARY_DIR,
# - architecture and system name are required to match for GETARCH_BINARY_DIR, but
# - uwp (aka WindowsStore) may run windows getarch.
string(REPLACE "WindowsStore_" "_" SYSTEM_KEY "${VCPKG_CMAKE_SYSTEM_NAME}_${VCPKG_TARGET_ARCHITECTURE}")
set(GETARCH_BINARY_DIR "${CURRENT_HOST_INSTALLED_DIR}/manual-tools/${PORT}/${SYSTEM_KEY}")
if(EXISTS "${GETARCH_BINARY_DIR}")
    message(STATUS "OpenBLAS cross build, but may use ${PORT}:${HOST_TRIPLET} getarch")
    list(APPEND OPTIONS "-DGETARCH_BINARY_DIR=${GETARCH_BINARY_DIR}")
elseif(VCPKG_CROSSCOMPILING)
    message(STATUS "OpenBLAS cross build, may not be able to use getarch")
else()
    message(STATUS "OpenBLAS native build")
endif()

if(VCPKG_TARGET_IS_EMSCRIPTEN)
    # Only the riscv64 kernel with riscv64_generic target is supported.
    # Cf. https://github.com/OpenMathLib/OpenBLAS/issues/3640#issuecomment-1144029630 et al.
    list(APPEND OPTIONS
        -DEMSCRIPTEN_SYSTEM_PROCESSOR=riscv64
        -DTARGET=RISCV64_GENERIC
    )
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${OPTIONS}
        "-DCMAKE_PROJECT_INCLUDE=${CURRENT_PORT_DIR}/cmake-project-include.cmake"
        -DBUILD_TESTING=OFF
        # tins-lib-native overlay: upstream vcpkg sets this ON, which drops LAPACK (incl.
        # SGESVD) entirely. tins-lib's SVD/PCA depends on OpenBLAS.SGESVD, so this must stay
        # OFF. NOFORTRAN=ON is kept -- OpenBLAS ships C-translated LAPACK sources for exactly
        # this no-Fortran-compiler case (how official OpenBLAS Windows DLLs get full LAPACK
        # without a real gfortran), so this combination does not need a Fortran toolchain.
        -DBUILD_WITHOUT_LAPACK=OFF
        -DNOFORTRAN=ON
        # Enabling LAPACK above pulls in lapack-netlib's own bundled CMakeLists.txt, which
        # (like fftw3's) predates CMake 3.5 and was never updated -- same CMake-4.x removal,
        # same documented escape hatch, see the fftw3 overlay's portfile.cmake for detail.
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        # OpenBLAS's generic SIMD abstraction layer (kernel/x86_64/../arm/sum.c via
        # simd/intrin_*.h) hits a longstanding, widely-reported OpenBLAS/GCC bug on Linux --
        # "inlining failed in call to 'always_inline' ... target specific option mismatch"
        # -- where the build system doesn't correctly attach matching -m<isa> flags to that
        # translation unit. Disabling AVX512 alone (confirmed) just shifted the identical
        # failure down to AVX2/FMA (_mm256_fmadd_ps, also confirmed in a real CI run), so
        # all three extended-width levels are disabled together here rather than
        # whack-a-moling one at a time -- this falls back to OpenBLAS's much more mature
        # hand-written assembly kernels wherever available. NO_AVX/NO_AVX2/NO_AVX512 are
        # OpenBLAS's own documented flags (cmake/system.cmake). This ties into the
        # already-deferred dynamic-arch/CPU-portability discussion (see CLAUDE.md) --
        # trading peak throughput for a build that actually completes, consistent with
        # that already-agreed direction, not a new one.
        -DNO_AVX=1
        -DNO_AVX2=1
        -DNO_AVX512=1
    MAYBE_UNUSED_VARIABLES
        GETARCH_BINARY_DIR
)

vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/OpenBLAS)
vcpkg_fixup_pkgconfig()

# Required from native builds, optional from cross builds.
if(NOT VCPKG_CROSSCOMPILING OR EXISTS "${CURRENT_PACKAGES_DIR}/bin/getarch${VCPKG_TARGET_EXECUTABLE_SUFFIX}")
    vcpkg_copy_tools(
        TOOL_NAMES getarch getarch_2nd 
        DESTINATION "${CURRENT_PACKAGES_DIR}/manual-tools/${PORT}/${SYSTEM_KEY}"
        AUTO_CLEAN
    )
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
