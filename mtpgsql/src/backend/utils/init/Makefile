#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for utils/init
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/utils/init/Makefile,v 1.1.1.1 2006/08/12 00:22:00 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = enbl.o findbe.o globals.o miscinit.o postinit.o

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

