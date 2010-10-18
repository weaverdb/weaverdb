#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for optimizer
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/optimizer/Makefile,v 1.1.1.1 2006/08/12 00:20:44 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ../..
include ../../Makefile.global

all: submake SUBSYS.o

OBJS = path/SUBSYS.o plan/SUBSYS.o prep/SUBSYS.o util/SUBSYS.o
# geqo/SUBSYS.o

DIRS = path plan prep util
# geqo

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

.PHONY: submake clean dep depend
submake:
	for i in $(DIRS); do $(MAKE) -C $$i SUBSYS.o; done

clean:
	rm -f SUBSYS.o
	for i in $(DIRS); do $(MAKE) -C $$i clean; done

.DEFAULT: 
	for i in $(DIRS); do $(MAKE) -C $$i $@; done
