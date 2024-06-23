#-------------------------------------------------------------------------
#
# Makefile
#    Makefile for src/bin (client programs)
#
# Copyright (c) 1994, Regents of the University of California
#
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/bin/Makefile,v 1.1.1.1 2006/08/12 00:11:17 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ..
include ../Makefile.global


.DEFAULT all:

all: comcheck create_db init_db reindex_db refresh_db start_db stop_db takesnapshot

install: all

create_db: 
	cat create_db.sh > create_db
	$(INSTALL) $(INSTL_EXE_OPTS) create_db $(BINDIR)/create_db

init_db: 
	cat init_db.sh > init_db 
	$(INSTALL) $(INSTL_EXE_OPTS) init_db $(BINDIR)/init_db

reindex_db:
	cat reindex_db.sh > reindex_db
	$(INSTALL) $(INSTL_EXE_OPTS) reindex_db $(BINDIR)/reindex_db

refresh_db:
	cat refresh_db.sh > refresh_db
	$(INSTALL) $(INSTL_EXE_OPTS) refresh_db $(BINDIR)/refresh_db

start_db:
	cat start_db.sh > start_db
	$(INSTALL) $(INSTL_EXE_OPTS) start_db $(BINDIR)/start_db

stop_db:
	cat stop_db.sh > stop_db
	$(INSTALL) $(INSTL_EXE_OPTS) stop_db $(BINDIR)/stop_db
	
takesnapshot:
	cat takesnapshot.sh > takesnapshot
	$(INSTALL) $(INSTL_EXE_OPTS) takesnapshot $(BINDIR)/takesnapshot

comcheck:
	cat comcheck.sh > comcheck
	$(INSTALL) $(INSTL_EXE_OPTS) comcheck $(BINDIR)/comcheck

clean:
	rm -f comcheck *_db takesnapshot

dep depend:

