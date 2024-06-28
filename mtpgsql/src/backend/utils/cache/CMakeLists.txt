
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(cache OBJECT ${SRC_FILES})

add_dependencies(cache build_fmgr)