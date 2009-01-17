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
JAVA_HOME=/usr/jdk/jdk1.5.0_12
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
-L${JAVA_HOME}/jre/lib/sparcv9/server \

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
	${CPP} ${CFLAGS} ${CFLAGS_SL} -DPG_JAVA \
	${MYINC} ${MYPATH} ${MYLIBS} -L. \
	-ljvm \
	main.cc \
	-o weaver
libweaver.so: WeaverStmtManager.cc WeaverFrameConnection.cc $(SRCDIR)/backend/libmtpg.a 
	${CPP} ${CFLAGS} ${CFLAGS_SL} -DPG_JAVA \
	-G \
	${MYINC} ${MYPATH} ${MYLIBS} -lmtpg -i \
	WeaverStmtManager.cc \
	WeaverFrameConnection.cc \
	-o libweaver.so
pgjava: libmtpgjava.so
	${CPP} ${CFLAGS} ${CFLAGS_SL} -DPG_JAVA \
	${MYINC} ${MYPATH} ${MYLIBS} -L. \
	-ljvm \
	main.cc \
	-o weaver
libmtpgjava.so: PostgresStmtManager.cc PostgresFrameConnection.cc $(SRCDIR)/backend/libmtpg.a 
	${CPP} ${CFLAGS} ${CFLAGS_SL} -DPG_JAVA \
	-G \
	${MYINC} ${MYPATH} ${MYLIBS} -lmtpg -i \
	PostgresStmtManager.cc \
	PostgresFrameConnection.cc \
	-o libmtpgjava.so
else
all: libweaver.jnilib weaver 
weaver: 
	g++ ${CFLAGS} -DPG_JAVA \
	${MACOSINC} ${MACOSPATH} -lpthread -framework JavaVM -lobjc \
	main.cc \
	-o weaver
libweaver.jnilib:
	g++ ${CFLAGS} -bundle -DMACOSX -DPG_JAVA \
	${MACOSINC} ${MACOSPATH} ${MACOSLIBS} -lmtpg \
	WeaverFrameConnection.cc \
	WeaverStmtManager.cc \
	-o libweaver.jnilib
endif

ifneq ($(PORTNAME),macosx)
clean_java:
	rm driver/weaver/*.class
	rm weaver.jar
clean: clean_java
	rm libweaver.so
	rm WeaverStmtManager.o
	rm WeaverFrameConnection.o
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
