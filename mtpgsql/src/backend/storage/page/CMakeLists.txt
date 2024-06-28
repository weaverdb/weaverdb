
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(page OBJECT ${SRC_FILES})

add_dependencies(page build_fmgr)