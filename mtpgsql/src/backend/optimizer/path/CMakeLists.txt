
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(path OBJECT ${SRC_FILES})

add_dependencies(path fmgrtab)
