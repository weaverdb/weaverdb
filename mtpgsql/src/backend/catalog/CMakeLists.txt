
# collect up the source files
get_property(YFLAGS GLOBAL PROPERTY YFLAGS)
get_property(BKIOPTS GLOBAL PROPERTY BKIOPTS)
get_property(INSTALL GLOBAL PROPERTY INSTALL)
get_property(INSTLOPTS GLOBAL PROPERTY INSTLOPTS)
get_property(SHOPTS GLOBAL PROPERTY SHOPTS)
get_property(CONFIGFILE GLOBAL PROPERTY CONFIGFILE)

set(TEMPLATEDIR ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})

file(MAKE_DIRECTORY ${TEMPLATEDIR})

set(GENBKI ./genbki.sh)

set(GLOBALBKI_SRCS pg_database.h pg_variable.h pg_shadow.h pg_group.h pg_log.h)

set(LOCALBKI_SRCS pg_proc.h pg_type.h pg_attribute.h pg_class.h 
                 pg_inherits.h pg_index.h pg_statistic.h 
                 pg_operator.h pg_opclass.h pg_am.h pg_amop.h pg_amproc.h 
                 pg_language.h 
                 pg_schema.h pg_trigger.h pg_extent.h pg_extstore.h 
                 pg_aggregate.h pg_ipl.h pg_inheritproc.h 
                 pg_rewrite.h pg_listener.h pg_description.h indexing.h 
                )

list(TRANSFORM GLOBALBKI_SRCS PREPEND ../../include/catalog/)
list(TRANSFORM LOCALBKI_SRCS PREPEND ../../include/catalog/)

add_custom_command(
    OUTPUT global1.bki.source global1.description local1_template1.bki.source local1_template1.description
    DEPENDS ${GLOBALBKI_SRCS} ${LOCALBKI_SRCS}
    COMMAND chmod 755 ${GENBKI}
    COMMAND env CONFIGFILE=${CONFIGFILE} CPP=${CMAKE_CXX_COMPILER} sh ${SHOPTS} ${GENBKI} ${BKIOPTS} ${GLOBALBKI_SRCS} > ${CMAKE_CURRENT_BINARY_DIR}/global1.bki.source 2> ${CMAKE_CURRENT_BINARY_DIR}/global1.description
    COMMAND env CONFIGFILE=${CONFIGFILE} CPP=${CMAKE_CXX_COMPILER} sh ${SHOPTS} ${GENBKI} ${BKIOPTS} ${LOCALBKI_SRCS} > ${CMAKE_CURRENT_BINARY_DIR}/local1_template1.bki.source 2> ${CMAKE_CURRENT_BINARY_DIR}/local1_template1.description
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_command( 
    OUTPUT ${TEMPLATEDIR}/global1.bki.source ${TEMPLATEDIR}/global1.description ${TEMPLATEDIR}/local1_template1.bki.source ${TEMPLATEDIR}/local1_template1.description
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/global1.bki.source ${CMAKE_CURRENT_BINARY_DIR}/global1.description ${CMAKE_CURRENT_BINARY_DIR}/local1_template1.bki.source ${CMAKE_CURRENT_BINARY_DIR}/local1_template1.description
    COMMAND ${INSTALL} ${INSTLOPTS} global1.bki.source ${TEMPLATEDIR}/global1.bki.source
    COMMAND ${INSTALL} ${INSTLOPTS} global1.description ${TEMPLATEDIR}/global1.description
    COMMAND ${INSTALL} ${INSTLOPTS} local1_template1.bki.source ${TEMPLATEDIR}/local1_template1.bki.source
    COMMAND ${INSTALL} ${INSTLOPTS} local1_template1.description ${TEMPLATEDIR}/local1_template1.description
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_library(catalog OBJECT catalog.c heap.c index.c indexing.c pg_aggregate.c pg_operator.c pg_proc.c pg_type.c
    ${TEMPLATEDIR}/global1.bki.source 
    ${TEMPLATEDIR}/global1.description 
    ${TEMPLATEDIR}/local1_template1.bki.source 
    ${TEMPLATEDIR}/local1_template1.description
)
