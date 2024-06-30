
include_directories(include)

option(NOARRAY "Don't build array functionality" ON)
option(ELOG_TIMESTAMPS "Add timestamps to the elog messages" ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(USE_ASSERT_CHECKING "Enable assert checking" ON)
    option(ABORT_ON_ASSERT "use abort on a failed assert" ON)
    option(MEMORY_CONTEXT_CHECKING "basic memory context sanity checks" ON)
    option(CLOBBER_FREED_MEMORY "Clobber freed memory" ON)
    
endif()

if (ANDROID)
    set(DEF_MAXBACKENDS 32 CACHE STRING "max number of backend connections")
    set(MAX_PRIVATE_FILES 4 CACHE STRING "max private files for the backend")
    set(BLCKSZ 4096 CACHE STRING "set the block size")
else()
    set(DEF_MAXBACKENDS 1024 CACHE STRING "max number of backend connections")
    set(MAX_PRIVATE_FILES 8 CACHE STRING "max private files for the backend")
    set(BLCKSZ 8192 CACHE STRING "set the block size")
endif()

configure_file(include/config.h.in include/config.h)

include_directories(${CMAKE_CURRENT_BINARY_DIR}/include/)

set_property(GLOBAL PROPERTY CONFIGFILE ${CMAKE_CURRENT_BINARY_DIR}/include/config.h)

set(BKIOPTS -I${CMAKE_CURRENT_BINARY_DIR}/include -I${CMAKE_CURRENT_SOURCE_DIR}/include)

if (NOARRAY)
    set(BKIOPTS ${BKIOPTS} -DNOARRAY)
endif()

set_property(GLOBAL PROPERTY BKIOPTS ${BKIOPTS})
set_property(GLOBAL PROPERTY YFLAGS -y -d)

get_filename_component(INSTALL "install-sh" ABSOLUTE)

set_property(GLOBAL PROPERTY INSTALL ${INSTALL})
set_property(GLOBAL PROPERTY INSTALL_EXE_OPTS -m 755)
set_property(GLOBAL PROPERTY INSTLOPTS -m 644)

add_custom_command(OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/env/timestamp.h
    COMMAND chmod 755 ./config.buildtime
    COMMAND ./config.buildtime
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_compile_definitions(_REENTRANT BYTE_ORDER=${CMAKE_C_BYTE_ORDER})

if (APPLE) 
    add_compile_definitions(MACOSX)
ENDIF()

add_subdirectory(backend)
add_subdirectory(bin)
