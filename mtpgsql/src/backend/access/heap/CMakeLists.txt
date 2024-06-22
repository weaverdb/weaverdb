
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(heap OBJECT ${SRC_FILES})

add_dependencies(heap build_fmgr)

target_compile_definitions(heap PUBLIC HEAP)