#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for storage/large_object
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/storage/large_object/Makefile,v 1.1.1.1 2006/08/12 00:21:27 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = inv_api.o

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

