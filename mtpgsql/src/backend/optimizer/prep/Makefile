#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for optimizer/prep
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/optimizer/prep/Makefile,v 1.1.1.1 2006/08/12 00:20:53 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = prepqual.o preptlist.o prepunion.o prepkeyset.o

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

