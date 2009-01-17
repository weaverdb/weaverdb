#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for utils/mmgr
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/utils/mmgr/Makefile,v 1.1.1.1 2006/08/12 00:22:02 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = aset.o subset.o mcxt.o portalmem.o

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

