#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for the access methods module
#
# IDENTIFICATION
#    $Header: /cvs/weaver/pgjava/Makefile,v 1.6 2007/02/04 07:39:14 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR=../mtpgsql/src
include ${SRCDIR}/Makefile.global

ifndef JAVA_HOME
JAVA_HOME=/usr/jdk/latest
endif

CFLAGS += -Wno-unused-variable -Wno-unused-function

ifeq ($(CC), CC)
SHARED=-G -Mmapfile.txt
else
SHARED=-shared -ld_classic -fvisibility=hidden -exported_symbols_list mac_mapfile.txt
endif

MYINC=\
-I${PG_HOME}/include -I./.  -I${PG_HOME}/backend -I${JAVA_HOME}/include \
-I${JAVA_HOME}/include/${PORTNAME} 
MACOSINC=\
-I${PG_HOME}/include -I./.  -I${PG_HOME}/backend -I${JAVA_HOME}/include 

ifeq ($(PORTNAME),solaris)
MYLIBS=\
-lpthread -lrt -lm -lsocket -lnsl -lc -lumem 
endif
ifeq ($(PORTNAME),linux)
MYLIBS=\
-lpthread -lrt -lm -lnsl -lc 
endif

MACOSLIBS=\
-lpthread 

MYPATH=\
-L${SRCDIR} \
-L${SRCDIR}/backend \
-L${JAVA_HOME}/lib/server \

MACOSPATH=\
-L${SRCDIR} \
-L${SRCDIR}/backend \
-L${JAVA_HOME}/lib \

# think about using malloc mtmalloc as memory management libs

ifneq ($(PORTNAME),darwin)
all: libweaver.so weaver
weaver: libweaver.so
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	${MYINC} ${MYPATH} ${MYLIBS} -L. \
	-ljvm \
	main.c \
	-o weaver
libweaver.so: WeaverStmtManager.c BaseWeaverConnection.c WeaverValueExtractor.c $(SRCDIR)/backend/libmtpg.a 
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	${SHARED} \
	${MYINC} ${MYPATH} ${MYLIBS} \
	WeaverStmtManager.c \
	BaseWeaverConnection.c \
	WeaverValueExtractor.c 	\
	-o libweaver.so -lmtpg
else
all: libweaver.dylib weaver
weaver: 
	${CC} ${CFLAGS} -DWEAVER_JAVA \
	${MACOSINC} ${MACOSPATH} -lpthread -L${JAVA_HOME}/lib/server/ -ljli -lobjc \
	main.c \
	-o weaver
libweaver.dylib:
	${CC} ${CFLAGS} ${SHARED}  -DWEAVER_JAVA \
	${MACOSINC} ${MACOSPATH} ${MACOSLIBS} -lmtpg \
	BaseWeaverConnection.c \
	WeaverValueExtractor.c \
	WeaverStmtManager.c \
	-o libweaver.dylib
endif

ifneq ($(PORTNAME),darwin)
clean: 
	rm libweaver.so
	rm weaver
clean_legacy:
	rm libmtpgjava.so
	rm PostgresStmtManager.o
	rm PostgresFrameConnection.o
	rm pgjava
else
clean: 
	rm -rf *.dSYM
	rm libweaver.dylib
	rm weaver
endif

ifneq ($(PORTNAME),darwin)
install: all
	cp libweaver.so ${POSTGRESDIR}/lib/libweaver.so
	cp weaver ${POSTGRESDIR}/bin/weaver
	ln -s weaver ${POSTGRESDIR}/bin/weaver_server
	cp build/libs/pgjava_c.jar ${POSTGRESDIR}/lib/weaver.jar
install_legacy: legacy
	cp libmtpgjava.so ${POSTGRESDIR}/lib/
	cp pgjava ${POSTGRESDIR}/bin/
	ln -s pgjava ${POSTGRESDIR}/bin/pgjava_server
else
install: all
	cp libweaver.dylib ${POSTGRESDIR}/lib/libweaver.dylib
	cp weaver ${POSTGRESDIR}/bin/weaver
	ln -s weaver ${POSTGRESDIR}/bin/weaver_server
	cp build/libs/pgjava_c.jar ${POSTGRESDIR}/lib/weaver.jar
endif
