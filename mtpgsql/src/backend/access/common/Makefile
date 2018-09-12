#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/common
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/access/common/Makefile,v 1.1.1.1 2006/08/12 00:19:55 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS+=-I../..

OBJS = heaptuple.o indextuple.o indexvalid.o printtup.o \
       scankey.o tupdesc.o  

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

heaptuple.o heapvalid.o tupdesc.o: ../../../include/fmgr.h

../../../include/fmgr.h:
	$(MAKE) -C ../.. fmgr.h

dep depend: ../../../include/fmgr.h
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS)

ifeq (depend,$(wildcard depend))
include depend
endif
