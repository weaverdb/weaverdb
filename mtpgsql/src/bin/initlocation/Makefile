#-------------------------------------------------------------------------
#
# Makefile.inc--
#    Makefile for bin/initlocation
#
# Copyright (c) 1994, Regents of the University of California
#
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/bin/initlocation/Makefile,v 1.1.1.1 2006/08/12 00:11:17 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ../..
include ../../Makefile.global

all: initlocation

initlocation: initlocation.sh
	cp -p initlocation.sh initlocation

install: initlocation
	$(INSTALL) $(INSTL_EXE_OPTS) $+ $(BINDIR)

clean:
	rm -f initlocation

dep depend:
