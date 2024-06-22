
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(hash OBJECT ${SRC_FILES})

target_compile_definitions(hash PUBLIC HASH)