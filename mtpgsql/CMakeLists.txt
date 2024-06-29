
add_subdirectory(src)

set(DBHOME dbhome.tar)

if (CMAKE_CROSSCOMPILING) 
        message(WARNING "cross compile, no dbhome")
else()
add_custom_target(dbhome 
    COMMAND ${CMAKE_COMMAND} -E rm -r -f -- dbhome
    COMMAND mtpg/bin/initdb ${PROJECT_BINARY_DIR}/dbhome
    COMMAND ${CMAKE_COMMAND} -E echo "delete from pg_shadow" | mtpg/bin/postgres -D ${PROJECT_BINARY_DIR}/dbhome template1
    COMMAND ${CMAKE_COMMAND} -E echo "build info block size ${BLCKSZ}" > ${PROJECT_BINARY_DIR}/dbhome/buildinfo.txt
    COMMAND ${CMAKE_COMMAND} -E tar cv ${DBHOME} dbhome
    COMMAND ${CMAKE_COMMAND} -E rm -r -f -- dbhome
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    BYPRODUCTS 
       ${PROJECT_BINARY_DIR}/${DBHOME}
    DEPENDS pg_version postgres pg_id initdb
    DEPENDS ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/global1.bki.source ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/global1.description ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/local1_template1.bki.source ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY}/local1_template1.description
)
endif()
