#-------------------------------------------------------------------------
#
# Makefile.inc--
#    Build and install postgres.
#
# Copyright (c) 1994, Regents of the University of California
#
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/GNUmakefile,v 1.7 2007/03/20 03:07:37 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= .
include Makefile.global

FIND = /usr/bin/find

# assuming gnu tar and split here
TAR  = /usr/sbin/tar
SPLIT = /usr/bin/split

ETAGS = /whs/SUNWspro/bin/etags
XARGS = /usr/bin/xargs

all:
	chmod 755 config.buildtime
	/bin/sh -e config.buildtime
	$(MAKE) -C utils all
	$(MAKE) -C backend all
	$(MAKE) -C interfaces all
	$(MAKE) -C bin all
	$(MAKE) -C ../../pgjava_c all
	@if test $@. = all. -o $@. = .; then \
	echo All of PostgreSQL is successfully made.  Ready to install. ;\
        fi

install: installdirs
	chmod 755 config.buildtime
	/bin/sh -e config.buildtime
	$(MAKE) -C utils install
	$(MAKE) -C backend install
	$(MAKE) -C interfaces install
	$(MAKE) -C bin install
	$(MAKE) -C scripts install
	$(MAKE) -C ../../pgjava_c install
	tar cvf $(POSTGRESDIR).tar $(POSTGRESDIR)
	bzip2 $(POSTGRESDIR).tar

installdirs: mkinstalldirs
	$(SRCDIR)/mkinstalldirs $(BINDIR) $(LIBDIR) $(INCLUDEDIR)

clean:
	$(MAKE) -C utils clean
	$(MAKE) -C backend clean
	$(MAKE) -C interfaces clean
	$(MAKE) -C bin clean
	$(MAKE) -C ../../pgjava_c clean
	rm -Rf tools
	rm -rf $(POSTGRESDIR)
	rm $(POSTGRESDIR).tar.bz2

installclean:
	rm -rf $(POSTGRESDIR)
	rm $(POSTGRESDIR).tar.bz2

.DEFAULT:
	chmod 755 config.buildtime
	/bin/sh -e config.buildtime
	$(MAKE) -C utils $@
	$(MAKE) -C backend $@
	$(MAKE) -C interfaces $@
	$(MAKE) -C bin $@
	$(MAKE) -C ../../pgjava_c $@
	@if test $@. = all. -o $@. = .; then \
	echo All of PostgreSQL is successfully made.  Ready to install. ;\
        fi

TAGS:
	rm -f TAGS; \
	for i in backend interfaces/libpq bin; do \
	  $(FIND) $$i -name '*.[chyl]' -print | $(XARGS) $(ETAGS) -a ; \
	done

# target to generate a backup tar file and split files that can be 
# saved to 1.44M floppy
BACKUP:
	rm -f BACKUP.filelist BACKUP.tgz; \
	$(FIND) . -not -path '*obj/*' -not -path '*data/*' -type f -print > BACKUP.filelist; \
	$(TAR) --files-from BACKUP.filelist -c -z -v -f BACKUP.tgz
	$(SPLIT) --bytes=1400k BACKUP.tgz pgBACKUP.	

.PHONY: TAGS
.PHONY: BACKUP

