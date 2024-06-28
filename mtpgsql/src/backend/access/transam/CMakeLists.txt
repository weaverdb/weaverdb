
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(transam OBJECT ${SRC_FILES})

target_compile_options(transam PRIVATE -Wno-format)

add_dependencies(transam build_fmgr)