#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for optimizer/util
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/optimizer/util/Makefile,v 1.1.1.1 2006/08/12 00:20:54 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = restrictinfo.o clauses.o indexnode.o plancat.o \
       joininfo.o pathnode.o relnode.o tlist.o var.o

# not ready yet: predmig.o xfunc.o

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

