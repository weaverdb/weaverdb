
cmake_minimum_required(VERSION 3.29.0)

project("weaverdb")

set(CMAKE_POSITION_INDEPENDENT_CODE on)
set(CMAKE_C_STANDARD 99)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/lib)

if (ANDROID)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
ENDIF ()

add_compile_definitions(PG_EXTERN=)

if (APPLE) 
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fgnu89-inline")
ENDIF()

add_subdirectory(mtpgsql)
add_subdirectory(pgjava_c)
