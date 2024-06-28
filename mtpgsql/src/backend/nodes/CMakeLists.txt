
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(nodes OBJECT ${SRC_FILES})

add_dependencies(nodes build_fmgr parser)