#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for postmaster
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/postmaster/Makefile,v 1.1.1.1 2006/08/12 00:21:16 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../..
include ../../Makefile.global

CFLAGS += -I..

OBJS = postmaster.o

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

