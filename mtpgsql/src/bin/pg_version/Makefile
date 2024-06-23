# Generated automatically from Makefile.in by configure.
#-------------------------------------------------------------------------
#
# Makefile.in--
#    Makefile for bin/pg_version
#
# Portions Copyright (c) 1996-2000, PostgreSQL, Inc
# Portions Copyright (c) 1994, Regents of the University of California
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/bin/pg_version/Makefile,v 1.1.1.1 2006/08/12 00:11:29 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ../..
include $(SRCDIR)/Makefile.global

CFLAGS+= -I$(SRCDIR)/include

OBJS= pg_version.o $(SRCDIR)/backend/utils/version.o 

all: pg_version

pg_version: $(OBJS)
	$(CC) $(CFLAGS) -o pg_version $(OBJS) $(LDFLAGS)

$(SRCDIR)/backend/utils/version.o: $(SRCDIR)/backend/utils/version.c $(SRCDIR)/include/version.h
	$(MAKE) -C $(SRCDIR)/backend/utils version.o

install: pg_version
	$(INSTALL) $(INSTL_EXE_OPTS) pg_version$(X) $(BINDIR)/pg_version$(X)

depend dep:
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f pg_version$(X) pg_version.o

ifeq (depend,$(wildcard depend))
include depend
endif
