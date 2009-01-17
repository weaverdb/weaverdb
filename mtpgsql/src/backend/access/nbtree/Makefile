#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/nbtree
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/access/nbtree/Makefile,v 1.1.1.1 2006/08/12 00:20:00 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../.. 

OBJS = nbtcompare.o nbtinsert.o nbtpage.o nbtree.o nbtsearch.o \
       nbtstrat.o nbtutils.o nbtsort.o

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

