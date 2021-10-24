#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for the access methods module
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/simpleserver/Makefile,v 1.1.1.1 2006/08/12 00:25:27 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR=..
include ../Makefile.global

PG_HOME=..

ifeq ($(PORTNAME), darwin)
ifndef JAVA_HOME
JAVA_HOME=/System/Library/Frameworks/JavaVM.framework
endif
else
ifndef JAVA_HOME
JAVA_HOME=$(HOME)/jdk
endif
endif

MYINC=\
-I${PG_HOME}/include -I./.  -I${PG_HOME}/backend -I${JAVA_HOME}/include \
-I${JAVA_HOME}/include/solaris
MACOSINC=\
-I${PG_HOME}/include -I./.  -I${PG_HOME}/backend -I${JAVA_HOME}/Headers 

MYLIBS=\
-lpthread -lrt -lsocket -lnsl -lc
MACOSLIBS=\
-lpthread 

MYPATH=\
-L${PG_HOME} \
-L${PG_HOME}/backend \
-L${JAVA_HOME}/jre/lib/sparc \
-L${JAVA_HOME}/jre/lib/sparcv9/server \
-L/usr/lib -L/usr/local/lib \

MACOSPATH=\
-L${PG_HOME} \
-L${PG_HOME}/backend \
-L${JAVA_HOME}/Libraries \
-L/usr/lib -L/usr/local/lib \

%.dylib: %.o
	$(LD) -bundle -o $@ $<
	
ifneq ($(PORTNAME),darwin)
all: libmtpgjava.so simpleserver.jar
# think about using malloc mtmalloc as memory management libs
pgjava: 
	${CC} ${CFLAGS} -DPG_JAVA \
	${MYINC} ${MYPATH} ${MYLIBS} -L. \
	-L/usr/local/lib \
	-lmalloc -ljvm \
	main.cc \
	-o pgjava
libmtpgjava.so: 
	${CC} ${CFLAGS} -DPG_JAVA \
	-G -Kpic  \
	${MYINC} ${MYPATH} ${MYLIBS} -lmtpg \
	org_mtpgsql_server_Connection.c \
	-o libmtpgjava.so
simpleserver.jar: 
	${JAVA_HOME}/bin/javac org/mtpgsql/server/*.java
	${JAVA_HOME}/bin/jar cvf simpleserver.jar org/mtpgsql/server/*.class
else
all: libmtpgjava.dylib simpleserver.jar
pgjava: 
	${CC} ${CFLAGS} -DPG_JAVA \
	${MACOSINC} ${MACOSPATH} -lpthread -framework JavaNativeFoundation -lobjc \
	-L/usr/local/lib \
	main.cc \
	-o pgjava
libmtpgjava.dylib:
	${CC} ${CFLAGS} -bundle $(CFLAGS) -DMACOSX -DPG_JAVA \
	${MACOSINC} ${MACOSPATH} ${MACOSLIBS} -lmtpg \
	org_mtpgsql_server_Connection.c \
	-o libmtpgjava.dylib
simpleserver.jar: 
	${JAVA_HOME}/Commands/javac org/mtpgsql/server/*.java
	${JAVA_HOME}/Commands/jar cvf simpleserver.jar org/mtpgsql/server/*.class
endif

ifneq ($(PORTNAME),darwin)
clean:
	rm libmtpgjava.so
	rm org_mtpgsql_server_Connection.o 
	rm simpleserver.jar
else
clean:
	rm libmtpgjava.dylib
	rm simpleserver.jar
endif

ifneq ($(PORTNAME),darwin)
install: all
	cp libmtpgjava.so ${POSTGRESDIR}/lib/libmtpgjava.so
	cp simpleserver.jar ${POSTGRESDIR}/lib/simpleserver.jar
	cp sstart ${POSTGRESDIR}/bin/mtpgstart
else
install: all
	cp libmtpgjava.dylib ${POSTGRESDIR}/lib/libmtpgjava.dylib
	cp simpleserver.jar ${POSTGRESDIR}/lib/simpleserver.jar
	cp mstart ${POSTGRESDIR}/bin/mtpgstart
endif
