
add_subdirectory(src)

add_custom_target(dbhome
    COMMAND ${CMAKE_COMMAND} -E rm -r -f -- dbhome
    COMMAND mtpg/bin/initdb ${PROJECT_BINARY_DIR}/dbhome
    COMMAND ${CMAKE_COMMAND} -E echo "delete from pg_shadow" | mtpg/bin/postgres -D ${PROJECT_BINARY_DIR}/dbhome template1
    COMMAND ${CMAKE_COMMAND} -E tar cv dbhome.tar dbhome
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    BYPRODUCTS 
       ${PROJECT_BINARY_DIR}/dbhome.tar
       ${PROJECT_BINARY_DIR}/dbhome 
    DEPENDS initdb pg_version postgres pg_id
)

add_custom_target(inittools
    COMMAND ${CMAKE_COMMAND} -E tar cv mtpg.tar mtpg/lib/global1* mtpg/lib/local1* mtpg/bin/*
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    BYPRODUCTS 
       ${PROJECT_BINARY_DIR}/mtpg.tar
    DEPENDS initdb pg_version postgres pg_id
)