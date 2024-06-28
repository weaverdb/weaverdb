
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(main OBJECT ${SRC_FILES})

add_dependencies(main build_fmgr)