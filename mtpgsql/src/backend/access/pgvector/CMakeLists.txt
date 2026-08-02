# pgvector access method integration for WeaverDB
# Collect sources for vector type + ivfflat/hnsw indexes.
# Include bit/half utils+vec too so that _PG_init symbols (BitvecInit etc) are present.
set(SRC_FILES
    pgvector_shims.c
    pgvector_pg7_am.c
    pgvector_cost.c
    vector.c
    halfvec.c
    halfutils.c
    sparsevec.c
    bitvec.c
    bitutils.c
    ivfflat.c
    ivfinsert.c
    ivfutils.c
    ivfkmeans.c
    ivfvacuum.c
    ivfbuild.c
    ivfscan.c
    hnswbuild.c
    hnswscan.c
    hnswutils.c
    hnswinsert.c
    hnswvacuum.c
    pgvector_executor_port.c
    pgvector_hnsw_stubs.c
    pgvector_session_vars.c
    pgvector_vacuum_stats.c
    pairingheap.c
)
# Upstream hnsw.c (modern GUC/reloptions/handler) is omitted; Weaver uses
# pgvector_pg7_am.c + pgvector_session_vars.c + pgvector_hnsw_stubs.c.

# create the library (as OBJECT so backend mtpg can link the objs)
add_library(pgvector OBJECT ${SRC_FILES})

add_dependencies(pgvector fmgrtab)

# Force-include our master compat header so that pgvector's many "modern" includes
# and identifier uses get shims without modifying the imported .c files.
# Works for Clang/GCC (AppleClang too). This is the key build-system glue.
if (CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
    target_compile_options(pgvector PRIVATE
        "-include${CMAKE_CURRENT_SOURCE_DIR}/port-stubs/pgvector_compat.h"
    )
else()
    # MSVC equiv would be /FI but we are on macOS primarily
    message(WARNING "pgvector build: force include of compat may need port for this compiler")
endif()

# port-stubs: pgvector-only overrides (amapi/reloptions/lwlock/cv + compat).
# Real ports live under src/include (lib/, port/, common/, utils/, access/parallel).
target_include_directories(pgvector BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/port-stubs
)
target_include_directories(pgvector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
)

# Compile definitions to adapt pgvector (written for PG13+) to this older Weaver PG fork.
# Note: PG_ macros and ereport→elog bridge are handled in pgvector_compat.h (force include).
target_compile_definitions(pgvector PRIVATE
    FLEXIBLE_ARRAY_MEMBER=
    PGDLLEXPORT=
)

# SIMD distance kernels (header-only: utils/simd/simd.h).
# Include path is direct because access/ is configured before utils/ creates the
# simd INTERFACE target. NEON is always available on arm64; AVX2/FMA for x86
# Release only (Debug stays plain -g).
target_include_directories(pgvector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../utils/simd
)

option(WEAVER_PGVECTOR_SIMD_NATIVE "Enable -mavx2/-mfma for pgvector on x86 Release" ON)
if(WEAVER_PGVECTOR_SIMD_NATIVE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64" OR
       (APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64"))
        include(CheckCCompilerFlag)
        check_c_compiler_flag("-mavx2" WEAVER_PGVECTOR_HAVE_MAVX2)
        check_c_compiler_flag("-mfma" WEAVER_PGVECTOR_HAVE_MFMA)
        if(WEAVER_PGVECTOR_HAVE_MAVX2)
            target_compile_options(pgvector PRIVATE -mavx2)
        endif()
        if(WEAVER_PGVECTOR_HAVE_MFMA)
            target_compile_options(pgvector PRIVATE -mfma)
        endif()
    endif()
endif()


