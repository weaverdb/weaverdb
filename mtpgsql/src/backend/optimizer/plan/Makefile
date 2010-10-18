#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for optimizer/plan
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/optimizer/plan/Makefile,v 1.1.1.1 2006/08/12 00:20:50 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..

OBJS = createplan.o initsplan.o planmain.o planner.o setrefs.o subselect.o

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

