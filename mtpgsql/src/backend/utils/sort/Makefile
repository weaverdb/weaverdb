#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for utils/sort
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/utils/sort/Makefile,v 1.1.1.1 2006/08/12 00:22:04 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = logtape.o tuplesort.o

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

