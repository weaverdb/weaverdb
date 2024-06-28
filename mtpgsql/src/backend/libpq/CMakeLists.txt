
# create the executable
add_library(libpq OBJECT be-dumpdata.c be-pqexec.c portal.c portalbuf.c 
	auth.c hba.c crypt.c password.c 
	pqcomm.c pqformat.c pqpacket.c pqsignal.c util.c
)

if (CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_compile_definitions(libpq PRIVATE ANDROID NOCRYPT)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    target_compile_definitions(libpq PRIVATE MACOSX NOCUSERID)
endif()

add_dependencies(libpq build_fmgr)