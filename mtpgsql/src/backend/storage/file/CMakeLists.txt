
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(file OBJECT ${SRC_FILES})

add_dependencies(file build_fmgr)