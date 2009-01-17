#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for utils/error
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/utils/error/Makefile,v 1.1.1.1 2006/08/12 00:21:58 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = assert.o elog.o exc.o excabort.o excid.o format.o

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

depend dep:
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS)

ifeq (depend,$(wildcard depend))
include depend
endif

