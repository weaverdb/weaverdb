#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for the storage manager subsystem
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/storage/Makefile,v 1.1.1.1 2006/08/12 00:21:23 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR= ../..
include ../../Makefile.global

all: submake SUBSYS.o

OBJS = buffer/SUBSYS.o file/SUBSYS.o ipc/SUBSYS.o \
       lmgr/SUBSYS.o page/SUBSYS.o smgr/SUBSYS.o 
#        large_object/SUBSYS.o 

DIRS = buffer file ipc lmgr page smgr 
# large_object 

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

.PHONY: submake clean dep

submake:
	for i in $(DIRS); do $(MAKE) -C $$i SUBSYS.o; done

clean:
	rm -f SUBSYS.o
	for i in $(DIRS); do $(MAKE) -C $$i clean; done

.DEFAULT: 
	for i in $(DIRS); do $(MAKE) -C $$i $@; done
