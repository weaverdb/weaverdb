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
ifndef JAVA_HOME
JAVA_HOME=/usr/jdk/latest
endif
endif

MYINC=\
-I${PG_HOME}/include -I./.  -I${PG_HOME}/backend -I${JAVA_HOME}/include \
-I${JAVA_HOME}/include/${PORTNAME} 
MACOSINC=\
-I${PG_HOME}/include -I./.  -I${PG_HOME}/backend -I${JAVA_HOME}/include 

ifeq ($(PORTNAME),solaris)
MYLIBS=\
-lpthread -lrt -lm -lsocket -lnsl -lc -lumem -lz
endif
ifeq ($(PORTNAME),linux)
MYLIBS=\
-lpthread -lrt -lm -lnsl -lc -lz
endif

MACOSLIBS=\
-lpthread 

MYPATH=\
-L${SRCDIR} \
-L${SRCDIR}/backend \
-L${JAVA_HOME}/jre/lib/${CPU}/server \

MACOSPATH=\
-L${SRCDIR} \
-L${SRCDIR}/backend \
-L${JAVA_HOME}/lib \

ifneq ($(PORTNAME),macosx)
all: java_bits libweaver.so weaver 
legacy: libmtpgjava.so pgjava 
# think about using malloc mtmalloc as memory management libs
java_bits:
	${JAVA_HOME}/bin/javac -g driver/weaver/*.java
	# ${JAVA_HOME}/bin/javac driver/weaver/*.java driver/weaver/jdbc/*.java
	${JAVA_HOME}/bin/jar cvf weaver.jar  driver/weaver/*.class 
	# ${JAVA_HOME}/bin/jar cvf weaver.jar  driver/weaver/*.class driver/weaver/jdbc/*.class
weaver: libweaver.so
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	${MYINC} ${MYPATH} ${MYLIBS} -L. \
	-ljvm \
	main.c \
	-o weaver
ifeq ($(PORTNAME),linux)
libweaver.so: WeaverStmtManager.c BaseWeaverConnection.c WeaverValueExtractor.c $(SRCDIR)/backend/libmtpg.a 
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	-G \
	${MYINC} ${MYPATH} ${MYLIBS} \
	WeaverStmtManager.c \
	BaseWeaverConnection.c \
	WeaverValueExtractor.c 	\
	-o libweaver.so -lmtpg
endif
ifeq ($(PORTNAME),solaris)
libweaver.so: WeaverStmtManager.c BaseWeaverConnection.c WeaverValueExtractor.c $(SRCDIR)/backend/libmtpg.a
	${CC} ${CFLAGS} ${CFLAGS_SL} -DWEAVER_JAVA \
	-G -Mmapfile.txt \
	${MYINC} ${MYPATH} ${MYLIBS} \
	WeaverStmtManager.c \
	BaseWeaverConnection.c \
	WeaverValueExtractor.c 	\
	-o libweaver.so -lmtpg
endif
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
