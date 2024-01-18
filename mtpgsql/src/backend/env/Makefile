#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for 
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/env/Makefile,v 1.2 2007/02/24 02:58:47 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../..
include ../../Makefile.global

INCLUDE_OPT = -I.. 

CFLAGS+=$(INCLUDE_OPT) -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
ifeq ($(CC), gcc) 
CFLAGS+= -Wno-unused-but-set-variable 
endif
OBJS = env.o dbwriter.o poolsweep.o freespace.o \
	vacuumlazy.o bitmapset.o \
	pg_crc.o analyze.o dolhelper.o delegatedscan.o \
	WeaverConnection.o FieldTransform.o connectionutil.o
# 	slotvar.o 

all: SUBSYS.o 
	
SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS) 

depend dep:
	$(CC) -MM $(INCLUDE_OPT) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS)

ifeq (depend,$(wildcard depend))
include depend
endif

