# SIMD utilities - mostly header-only for intrinsics.
# Consumers get include path via this INTERFACE library.
# Optional architecture flags apply to dependents that link simd.

add_library(simd INTERFACE)
target_include_directories(simd INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Sensible defaults: enable AVX2+FMA on x86 when the compiler supports it,
# but only for non-Debug builds so Debug stays simple (-g, no -mavx2).
# arm64/Apple Silicon already has NEON without extra flags.
option(WEAVER_SIMD_NATIVE "Pass -mavx2/-mfma (x86) to SIMD consumers in Release" ON)

if(WEAVER_SIMD_NATIVE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64" OR
     (APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64"))
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-mavx2" WEAVER_HAVE_MAVX2)
    check_c_compiler_flag("-mfma" WEAVER_HAVE_MFMA)
    if(WEAVER_HAVE_MAVX2)
      target_compile_options(simd INTERFACE -mavx2)
    endif()
    if(WEAVER_HAVE_MFMA)
      target_compile_options(simd INTERFACE -mfma)
    endif()
  endif()
endif()
