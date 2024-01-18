#-------------------------------------------------------------------------
#
# Makefile
#    Makefile for bin/pg_id
#
# Copyright (C) 2000 by PostgreSQL Global Development Team
#
# $Header: /cvs/weaver/mtpgsql/src/bin/maccleaner/Makefile,v 1.1.1.1 2006/08/12 00:11:18 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ../..
include ../../Makefile.global

OBJS= maccleaner.o

all: maccleaner

maccleaner: $(OBJS)
	$(CC) -o macclean $(OBJS) $(LDFLAGS)

install: maccleaner
	$(INSTALL) $(INSTL_EXE_OPTS) macclean $(BINDIR)/macclean

depend dep:
	$(CC) -MM $(CFLAGS) *.c >depend

clean:
	rm -f macclean $(OBJS)

ifeq (depend,$(wildcard depend))
include depend
endif
