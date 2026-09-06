
add_subdirectory(src)

# Test-only crash injector (LD_PRELOAD / DYLD_INSERT_LIBRARIES) and the
# multiuser embed SQL driver used by the crash-consistency harness.
# Neither is linked into production libraries or the postgres executable.
if (NOT CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT CMAKE_CROSSCOMPILING)
	add_subdirectory(src/test/crash_injector)
	add_subdirectory(src/test/embed_sql)
	add_dependencies(postgres weaver_crash_injector weaver_embed_sql)
endif()

set(DBHOME dbhome.tar)

if (CMAKE_CROSSCOMPILING) 
        message(WARNING "cross compile, no dbhome")
else()
add_custom_target(dbhome 
    COMMAND ${CMAKE_COMMAND} -E rm -r -f -- dbhome
    COMMAND mtpg/bin/initdb -a ${PROJECT_BINARY_DIR}/dbhome
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
