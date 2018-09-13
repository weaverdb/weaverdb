#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for nodes
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/nodes/Makefile,v 1.1.1.1 2006/08/12 00:20:40 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../..
include ../../Makefile.global

CFLAGS += -I.. -Wno-switch
ifeq ($(CC), gcc) 
CFLAGS += -Wno-unused-but-set-variable 
endif
OBJS = nodeFuncs.o nodes.o list.o \
       copyfuncs.o equalfuncs.o freefuncs.o makefuncs.o outfuncs.o \
       readfuncs.o print.o read.o

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

outfuncs.o: ../parse.h

../parse.h:
	$(MAKE) -C .. parse.h

depend dep: ../parse.h
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS) 

ifeq (depend,$(wildcard depend))
include depend
endif

