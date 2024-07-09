
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(prep OBJECT ${SRC_FILES})

add_dependencies(prep fmgrtab)