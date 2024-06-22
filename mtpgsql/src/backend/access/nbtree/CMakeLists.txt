
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(nbtree OBJECT ${SRC_FILES})

target_compile_definitions(nbtree PUBLIC NBTREE)