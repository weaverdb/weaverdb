
cmake_minimum_required(VERSION 3.29.0)

project("weaverdb")

set(CMAKE_POSITION_INDEPENDENT_CODE on)
set(CMAKE_C_STANDARD 99)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/lib)

add_compile_definitions(PG_EXTERN=)

if (APPLE) 
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fgnu89-inline")
ENDIF()

add_subdirectory(mtpgsql)
add_subdirectory(pgjava_c)
