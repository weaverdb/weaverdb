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

ifeq ($(PORTNAME), macosx)
JAVA_HOME=/Library/Java/Home
else
JAVA_HOME=/usr/jdk/latest
endif

MYINC=\
-I${PG_HOME}/include -I./.  -I${PG_HOME}/backend -I${JAVA_HOME}/include \
-I${JAVA_HOME}/include/solaris 
MACOSINC=\
-I${PG_HOME}/include -I./.  -I${PG_HOME}/backend -I${JAVA_HOME}/include 

MYLIBS=\
-lpthread -lrt -lm -lsocket -lnsl -lc -lumem

MACOSLIBS=\
-lpthread 

MYLIBSOLD=\
-ldl -lc -lm -lthread -lw -lrt
MYPATH=\
-L${SRCDIR} \
-L${SRCDIR}/backend \
-L${JAVA_HOME}/jre/lib/amd64/server \

MACOSPATH=\
-L${SRCDIR} \
-L${SRCDIR}/backend \
-L${JAVA_HOME}/lib \

ifneq ($(PORTNAME),macosx)
all: libweaver.so weaver java_bits
legacy: libmtpgjava.so pgjava 
# think about using malloc mtmalloc as memory management libs
java_bits:
	${JAVA_HOME}/bin/javac driver/weaver/*.java
	# ${JAVA_HOME}/bin/javac driver/weaver/*.java driver/weaver/jdbc/*.java
	${JAVA_HOME}/bin/jar cvf weaver.jar  driver/weaver/*.class 
	# ${JAVA_HOME}/bin/jar cvf weaver.jar  driver/weaver/*.class driver/weaver/jdbc/*.class
weaver: libweaver.so
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	${MYINC} ${MYPATH} ${MYLIBS} -L. \
	-ljvm \
	main.c \
	-o weaver
libweaver.so: WeaverStmtManager.c BaseWeaverConnection.c WeaverValueExtractor.c $(SRCDIR)/backend/libmtpg.a 
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	-G \
	${MYINC} ${MYPATH} ${MYLIBS} -lmtpg -i \
	WeaverStmtManager.c \
	BaseWeaverConnection.c \
	WeaverValueExtractor.c 	\
	-o libweaver.so
pgjava: libmtpgjava.so
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	${MYINC} ${MYPATH} ${MYLIBS} -L. \
	-ljvm \
	main.c \
	-o weaver
libmtpgjava.so: PostgresStmtManager.c PostgresFrameConnection.c $(SRCDIR)/backend/libmtpg.a 
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	-G \
	${MYINC} ${MYPATH} ${MYLIBS} -lmtpg -i \
	PostgresStmtManager.c \
	PostgresFrameConnection.c \
	-o libmtpgjava.so
else
all: libweaver.jnilib weaver 
weaver: 
	g++ ${CFLAGS} -DWEAVER_JAVA \
	${MACOSINC} ${MACOSPATH} -lpthread -framework JavaVM -lobjc \
	main.c \
	-o weaver
libweaver.jnilib:
	g++ ${CFLAGS} -bundle -DMACOSX -DWEAVER_JAVA \
	${MACOSINC} ${MACOSPATH} ${MACOSLIBS} -lmtpg \
	BaseWeaverConnection.c \
	WeaverValueExtractor.c \
	WeaverStmtManager.c \
	-o libweaver.jnilib
endif

ifneq ($(PORTNAME),macosx)
clean_java:
	rm driver/weaver/*.class
	rm weaver.jar
clean: clean_java
	rm libweaver.so
	rm WeaverStmtManager.o
	rm BaseWeaverConnection.o
	rm WeaverValueExtractor.o
	rm weaver
clean_legacy:
	rm libmtpgjava.so
	rm PostgresStmtManager.o
	rm PostgresFrameConnection.o
	rm pgjava
else
clean:
	rm libweaver.jnilib
	rm weaver
endif

ifneq ($(PORTNAME),macosx)
install: all
	cp libweaver.so ${POSTGRESDIR}/lib/libweaver.so
	cp weaver ${POSTGRESDIR}/bin/weaver
	ln -s weaver ${POSTGRESDIR}/bin/weaver_server
	cp weaver.jar ${POSTGRESDIR}/lib/weaver.jar
install_legacy: legacy
	cp libmtpgjava.so ${POSTGRESDIR}/lib/
	cp pgjava ${POSTGRESDIR}/bin/
	ln -s pgjava ${POSTGRESDIR}/bin/pgjava_server
else
install: all
	cp libweaver.jnilib ${POSTGRESDIR}/lib/libweaver.jnilib
	cp weaver ${POSTGRESDIR}/bin/weaver
	ln -s weaver ${POSTGRESDIR}/bin/weaver_server
endif
