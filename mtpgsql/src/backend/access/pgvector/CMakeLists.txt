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
)
# pgvector_ivf_stubs.c replaced by ivfbuild.c / ivfscan.c when PG7 port complete.
# hnsw.c excluded (modern GUC/reloptions/cost); pgvector_hnsw_stubs.c holds env/init only.

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

# Include paths: port-stubs only for pgvector-only shims (no duplicate of src/include).
target_include_directories(pgvector BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/port-stubs
)
target_include_directories(pgvector PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
)

# Compile definitions to adapt pgvector (written for PG13+) to this older Weaver PG fork.
# Note: many PG_ macro neuters and ereport now handled in pgvector_compat.h (force include)
# for reliability. Keep here only the core version/flex/etc.
target_compile_definitions(pgvector PRIVATE
    FLEXIBLE_ARRAY_MEMBER=
    PGDLLEXPORT=
    ERRCODE_DATA_EXCEPTION=0
)

# Ensure the port-stubs dir is visible (redundant with BEFORE but harmless)
target_include_directories(pgvector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/port-stubs)
