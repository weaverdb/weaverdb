
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(rewrite OBJECT ${SRC_FILES})