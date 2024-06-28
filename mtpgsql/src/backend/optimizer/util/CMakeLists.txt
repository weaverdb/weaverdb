
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(util OBJECT ${SRC_FILES})

add_dependencies(util build_fmgr)