
add_subdirectory(src)

set(DBHOME dbhome.tar)

if (CMAKE_CROSSCOMPILING) 
    add_custom_target(dbhome 
        COMMAND ${CMAKE_COMMAND} -E echo "cross compile, no dbhome")
else()
add_custom_target(dbhome 
    COMMAND ${CMAKE_COMMAND} -E rm -r -f -- dbhome
    COMMAND mtpg/bin/initdb ${PROJECT_BINARY_DIR}/dbhome
    COMMAND ${CMAKE_COMMAND} -E echo "delete from pg_shadow" | mtpg/bin/postgres -D ${PROJECT_BINARY_DIR}/dbhome template1
    COMMAND ${CMAKE_COMMAND} -E tar cv ${DBHOME} dbhome
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    BYPRODUCTS 
       ${PROJECT_BINARY_DIR}/${DBHOME}
       ${PROJECT_BINARY_DIR}/dbhome 
    DEPENDS initdb pg_version postgres pg_id anontemplates
)
endif()
