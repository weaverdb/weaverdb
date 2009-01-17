#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/rtree
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/access/rtree/Makefile,v 1.1.1.1 2006/08/12 00:20:04 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../.. 

OBJS = rtget.o rtproc.o rtree.o rtscan.o rtstrat.o

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

