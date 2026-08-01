find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

get_property(YFLAGS GLOBAL PROPERTY YFLAGS)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/parse.h ${CMAKE_CURRENT_SOURCE_DIR}/gram.c ${CMAKE_CURRENT_SOURCE_DIR}/../../include/parser/parse.h
    COMMAND ${BISON_EXECUTABLE} ${YFLAGS} gram.y
    COMMAND  ${CMAKE_COMMAND} -E rename y.tab.c gram.c
    COMMAND ${CMAKE_COMMAND} -E rename y.tab.h parse.h
    COMMAND ${CMAKE_COMMAND} -E copy parse.h ../../include/parser/
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/scan.c
    COMMAND ${FLEX_EXECUTABLE} scan.l
    COMMAND ${CMAKE_COMMAND} -E rename lex.yy.c scan.c
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)

# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(parser OBJECT ${SRC_FILES} gram.c scan.c parse.h)

# gram/scan generation can race with other targets that need fmgr.h
add_dependencies(parser fmgrtab)

target_include_directories(parser PRIVATE .)
