
cmake_minimum_required(VERSION 4.2.1)

project("weaverdb" C)

if (WIN32)
    message(FATAL_ERROR
        "Windows is not currently a supported native build platform for WeaverDB. "
        "The engine assumes POSIX APIs (pthread, SysV/private shm, Unix paths) and "
        "LP64 long/pointer sizes. See README.md for supported platforms.")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE on)
set(CMAKE_C_STANDARD 99)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/mtpg/lib)

find_package(Threads REQUIRED)

if (ANDROID)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
ENDIF ()

add_compile_definitions(PG_EXTERN=)

if (APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fgnu89-inline")
elseif (CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    # Match historical GNU89 inline expectations still present in some headers.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fgnu89-inline")
ENDIF()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Expose POSIX/BSD APIs (strdup, etc.) under C99 on glibc.
    add_compile_definitions(LINUX _GNU_SOURCE)
endif()

add_subdirectory(mtpgsql)
add_subdirectory(pgjava_c)
