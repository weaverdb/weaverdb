
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(init OBJECT ${SRC_FILES})

target_include_directories(init PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../access/pgvector)

add_dependencies(init fmgrtab)