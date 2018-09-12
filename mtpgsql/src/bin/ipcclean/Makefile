#-------------------------------------------------------------------------
#
# Makefile for bin/ipcclean
#
# Copyright (c) 1994, Regents of the University of California
#
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/bin/ipcclean/Makefile,v 1.1.1.1 2006/08/12 00:11:17 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ../..
include ../../Makefile.global

all: ipcclean

ipcclean: ipcclean.sh
	cp $< $@

install: ipcclean
	$(INSTALL) $(INSTL_EXE_OPTS) $^ $(BINDIR)

clean:
	rm -f ipcclean

dep depend:
