find_package(BISON)
find_package(FLEX)

get_property(YFLAGS GLOBAL PROPERTY YFLAGS)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/bootstrap_tokens.h ${CMAKE_CURRENT_SOURCE_DIR}/bootparse.c
    COMMAND ${BISON_EXECUTABLE} ${YFLAGS} bootparse.y
    COMMAND grep -v "^#" boot.sed > sedfile
    COMMAND sed -f sedfile < y.tab.c > bootparse.c
    COMMAND ${CMAKE_COMMAND} -E rename y.tab.h bootstrap_tokens.h
    COMMAND ${CMAKE_COMMAND} -E remove -f -- y.tab.c sedfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)

add_custom_command(
  OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/bootscanner.c
    COMMAND ${FLEX_EXECUTABLE} bootscanner.l
    COMMAND grep -v "^#" boot.sed > sedfile
    COMMAND ${CMAKE_COMMAND} -E rename lex.Int_yy.c lex.yy.c
    COMMAND sed -f sedfile < lex.yy.c > bootscanner.c
    COMMAND ${CMAKE_COMMAND} -E remove -f -- lex.yy.c sedfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM)

# create the executable
add_library(bootstrap OBJECT bootparse.c bootstrap.c bootscanner.c)
