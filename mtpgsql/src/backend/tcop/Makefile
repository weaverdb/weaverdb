#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for tcop
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/tcop/Makefile,v 1.1.1.1 2006/08/12 00:21:32 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ../..
include ../../Makefile.global

CFLAGS+= -I..

ifeq ($(CC), gcc)
CFLAGS+= -Wno-error
endif

OBJS= dest.o fastpath.o pquery.o utility.o postgres.o

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

# The following dependencies are picked up by the make dep, but since 
# not everyone can do make dep, and these are particularly important
# dependencies (because they don't even exist until you make them),
# they are hardcoded here.

utility.o: ../parse.h ../../include/fmgr.h
aclchk.o: ../../include/fmgr.h
fastpath.o: ../../include/fmgr.h
postgres.o: ../../include/fmgr.h

../parse.h: 
	$(MAKE) -C .. parse.h

../../include/fmgr.h: 
	$(MAKE) -C .. fmgr.h

dep depend: ../parse.h ../../include/fmgr.h
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS) 

ifeq (depend,$(wildcard depend))
include depend
endif

