
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(rtree OBJECT ${SRC_FILES})

