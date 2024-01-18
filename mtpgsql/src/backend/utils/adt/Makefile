#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for utils/adt
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/utils/adt/Makefile,v 1.1.1.1 2006/08/12 00:21:37 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../../..
include ../../../Makefile.global

CFLAGS += -I../.. -Wno-unused-variable -Wno-unused-but-set-variable
ifeq ($(CC), gcc)
CFLAGS += -Wno-format-overflow 
endif

# seems to be required for some date/time stuff 1999/07/22 bjm
ifeq ($(CPU),alpha)
ifeq ($(CC), gcc)
CFLAGS+= -mieee
endif
ifeq ($(CC), egcs)
CFLAGS+= -mieee
endif
endif

OBJS = bool.o cash.o char.o \
	date.o datetime.o datum.o filename.o float.o \
	int.o int8.o like.o lztext.o \
	misc.o nabstime.o name.o not_in.o numeric.o numutils.o \
	oid.o  \
	regexp.o regproc.o \
    selfuncs.o sets.o \
	tid.o timestamp.o varbit.o varchar.o varlena.o version.o \
	network.o mac.o inet_net_ntop.o inet_net_pton.o \
	pg_lzcompress.o pg_locale.o formatting.o \
    ruleutils.o ri_triggers.o java.o wrapdatum.o md5.o sha2.o

ifdef USEACL
OBJS += acl.o oracle_compat.o
endif

ifdef USEARRAY
OBJS += arrayfuncs.o arrayutils.o chunk.o
endif

ifdef USEGEO
OBJS += geo_ops.o geo_selfuncs.o
endif
 
all: SUBSYS.o

formatting.o:
	${CC} ${CFLAGS} -I../../../include \
	-I../../ -c -o formatting.o formatting.c

SUBSYS.o: $(OBJS) 
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

depend dep:
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS)

ifeq (depend,$(wildcard depend))
include depend
endif



