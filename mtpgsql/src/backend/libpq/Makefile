#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for libpq subsystem (backend half of libpq interface)
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/libpq/Makefile,v 1.1.1.1 2006/08/12 00:20:36 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../..
include ../../Makefile.global

CFLAGS += -I..

# kerberos flags
ifdef KRBVERS
CFLAGS+= $(KRBFLAGS)
LDFLAGS+= $(KRBLIBS)
endif

OBJS = be-dumpdata.o be-pqexec.o portal.o portalbuf.o \
	auth.o hba.o crypt.o password.o \
	pqcomm.o pqformat.o pqpacket.o pqsignal.o util.o
# be-fsstubs.o

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

be-dumpdata.o be-pqexec.o: ../../include/fmgr.h

../../include/fmgr.h: 
	$(MAKE) -C .. fmgr.h

depend dep:
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS) 

ifeq (depend,$(wildcard depend))
include depend
endif

