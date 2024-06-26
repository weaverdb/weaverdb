
add_subdirectory(src)

add_custom_target(dbhome ALL
    COMMAND ${CMAKE_COMMAND} -E rm -r -f -- dbhome
    COMMAND mtpg/bin/initdb -D ${PROJECT_BINARY_DIR}/dbhome
    COMMAND ${CMAKE_COMMAND} -E tar cv dbhome.tar dbhome
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    BYPRODUCTS 
       ${PROJECT_BINARY_DIR}/dbhome.tar
       ${PROJECT_BINARY_DIR}/dbhome 
    DEPENDS initdb pg_version postgres pg_id
)
