#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/index
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/access/index/Makefile,v 1.1.1.1 2006/08/12 00:20:00 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../.. 

OBJS = genam.o indexam.o istrat.o

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

