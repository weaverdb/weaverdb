#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/hash
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/access/hash/Makefile,v 1.1.1.1 2006/08/12 00:19:57 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = hash.o hashfunc.o hashinsert.o hashovfl.o hashpage.o hashscan.o \
       hashsearch.o hashstrat.o hashutil.o

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

