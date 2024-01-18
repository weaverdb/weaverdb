#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for storage/buffer
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/storage/buffer/Makefile,v 1.1.1.1 2006/08/12 00:21:24 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../..
CFLAGS += -Wno-unused-but-set-variable


OBJS = buf_table.o buf_init.o bufmgr.o freelist.o localbuf.o m_lock.o

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

depend dep:
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS) s_lock_test

s_lock_test: s_lock.c
	$(CC) $(CFLAGS) -DS_LOCK_TEST=1 s_lock.c -o s_lock_test
	./s_lock_test

ifeq (depend,$(wildcard depend))
include depend
endif
