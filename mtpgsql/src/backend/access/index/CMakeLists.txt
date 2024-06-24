
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(index OBJECT ${SRC_FILES})

