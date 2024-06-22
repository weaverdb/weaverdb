
# create the executable
add_library(libpq OBJECT be-dumpdata.c be-pqexec.c portal.c portalbuf.c 
	auth.c hba.c crypt.c password.c 
	pqcomm.c pqformat.c pqpacket.c pqsignal.c util.c
)