
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(misc OBJECT ${SRC_FILES})

add_dependencies(misc fmgrtab)