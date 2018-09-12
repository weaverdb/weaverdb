#-------------------------------------------------------------------------
#
# Makefile
#    Makefile for bin/pg_id
#
# Copyright (C) 2000 by PostgreSQL Global Development Team
#
# $Header: /cvs/weaver/mtpgsql/src/bin/pg_id/Makefile,v 1.1.1.1 2006/08/12 00:11:28 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ../..
include ../../Makefile.global

OBJS= pg_id.o

all: pg_id

pg_id: $(OBJS)
	$(CC) ${CFLAGS} -o pg_id $(OBJS) $(LDFLAGS)

install: pg_id
	$(INSTALL) $(INSTL_EXE_OPTS) pg_id$(X) $(BINDIR)/pg_id$(X)

depend dep:
	$(CC) -MM $(CFLAGS) *.c >depend

clean:
	rm -f pg_id$(X) $(OBJS)

ifeq (depend,$(wildcard depend))
include depend
endif
