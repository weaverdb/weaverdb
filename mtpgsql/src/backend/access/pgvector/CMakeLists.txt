# pgvector access method integration for WeaverDB
# Collect sources for vector type + ivfflat/hnsw indexes.
# Include bit/half utils+vec too so that _PG_init symbols (BitvecInit etc) are present.
set(SRC_FILES
    pgvector_shims.c
    vector.c
    sparsevec.c
)
# NOTE: full ivfflat + hnsw sources have deep dependencies on PG AM/optimizer/lock/guc internals
# not present in this Weaver fork. Core vector type + sparse build here for the build system
# integration. Index AM files can be enabled after more stubs or when executor/catalog wiring is ready.
# bit/half .c excluded to avoid f16 dispatch / varbit complexity on this fork;
# dummy inits provided in shims.c so vector.c _PG_init links.

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

# Include paths: port-stubs must come BEFORE the real src/include so that
# our shims (varatt.h, utils/float.h, common/shortest_dec.h, etc) are found first.
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
    PG_VERSION_NUM=130000
    FLEXIBLE_ARRAY_MEMBER=
    PGDLLEXPORT=
    ERRCODE_DATA_EXCEPTION=0
)

# Ensure the port-stubs dir is visible (redundant with BEFORE but harmless)
target_include_directories(pgvector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/port-stubs)