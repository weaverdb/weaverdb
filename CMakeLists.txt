
cmake_minimum_required(VERSION 3.29.0)

project("weaverdb")

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(C_STANDARD 89)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fPIC -m64 -std=gnu89")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/lib)

add_subdirectory(mtpgsql)
add_subdirectory(pgjava_c)
