#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for tioga
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/tioga/Makefile,v 1.1.1.1 2006/08/12 00:21:34 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../..
include ../../Makefile.global

INCLUDE_OPT = -I.. 

CFLAGS+=$(INCLUDE_OPT)

OBJS = tgRecipe.o Varray.o

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

