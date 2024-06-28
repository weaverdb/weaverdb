
# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(ipc OBJECT ${SRC_FILES})

add_dependencies(ipc build_fmgr)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_compile_definitions(ipc PRIVATE ANDROID PRIVATEONLY)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(ipc PRIVATE MACOSX PRIVATEONLY)
endif()