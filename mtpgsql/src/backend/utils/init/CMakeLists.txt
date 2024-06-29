
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(init OBJECT ${SRC_FILES})

add_dependencies(init fmgrtab)