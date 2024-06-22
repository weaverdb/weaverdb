find_package(BISON)
find_package(FLEX)

get_property(YFLAGS GLOBAL PROPERTY YFLAGS)
message("test" ${YFLAGS})

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
    COMMAND mv lex.yy.c scan.c
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)

# collect up the source files
file(GLOB SRC_FILES "*.c")

# create the executable
add_library(parser OBJECT ${SRC_FILES} gram.c scan.c parse.h)

target_include_directories(parser PRIVATE .)
