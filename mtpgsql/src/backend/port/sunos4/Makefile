#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for port/sparc
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/port/sunos4/Makefile,v 1.1.1.1 2006/08/12 00:21:16 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

INCLUDE_OPT = -I../.. 

CFLAGS+=$(INCLUDE_OPT)

OBJS = strtol.o

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

depend dep:
	$(CC) -MM $(INCLUDE_OPT) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS)

ifeq (depend,$(wildcard depend))
include depend
endif

