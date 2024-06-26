
include_directories(include)

set_property(GLOBAL PROPERTY BKIOPTS -DNOARRAY -I../../include -I..)
set_property(GLOBAL PROPERTY YFLAGS -y -d)

get_filename_component(INSTALL "install-sh" ABSOLUTE)

message(WARNING "Absolute install:" ${INSTALL})

set_property(GLOBAL PROPERTY INSTALL ${INSTALL})
set_property(GLOBAL PROPERTY INSTALL_EXE_OPTS -m 755)
set_property(GLOBAL PROPERTY INSTLOPTS -m 644)


add_custom_target(timestamp
    COMMAND chmod 755 ./config.buildtime
    COMMAND ./config.buildtime
      BYPRODUCTS include/env/timestamp.h 
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)

add_compile_definitions(NOARRAY _REENTRANT BYTE_ORDER=${CMAKE_C_BYTE_ORDER})

if (APPLE) 
    add_compile_definitions(MACOSX)
ENDIF()

add_subdirectory(backend)
add_subdirectory(bin)
