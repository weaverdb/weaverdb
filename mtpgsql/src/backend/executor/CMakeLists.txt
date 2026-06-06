
find_package(JNI REQUIRED)


# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(executor OBJECT ${SRC_FILES})

add_dependencies(executor JNI::JNI fmgrtab)

if (JNI_FOUND)
    message ("JNI found")
    target_include_directories(executor PRIVATE ${JNI_INCLUDE_DIRS})
endif()

# SIMD helpers for high-impact acceleration in executor (aggs, scans, etc.)
target_include_directories(executor PRIVATE ../utils/simd)

# pgvector headers so executor/plan code can see vector types / index AM declarations
# (port-stubs intentionally *not* added here to avoid shadowing real headers like utils/varbit.h)
target_include_directories(executor PRIVATE ../access/pgvector)