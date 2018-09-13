#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for commands
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/backend/commands/Makefile,v 1.1.1.1 2006/08/12 00:20:17 synmscott Exp $
#
#-------------------------------------------------------------------------

SRCDIR = ../..
include ../../Makefile.global

CFLAGS += -I.. -Wno-unused-variable
ifeq ($(CC), gcc) 
CFLAGS += -Wno-unused-but-set-variable 
endif
OBJS = async.o creatinh.o command.o comment.o copy.o indexcmds.o define.o \
       remove.o rename.o  view.o cluster.o \
       explain.o sequence.o trigger.o user.o proclang.o \
       dbcommands.o variable.o
#vacuum.o

all: SUBSYS.o

SUBSYS.o: $(OBJS)
	$(LD) $(LDREL) $(LDOUT) SUBSYS.o $(OBJS)

# The following declares a hard-coded dependency on parse.h since, 
# if compiled without make dep, comment.c would get compiled before 
# the parser. 

comment.o: ../parse.h

../parse.h:
	$(MAKE) -C .. parse.h

depend dep:
	$(CC) -MM $(CFLAGS) *.c >depend

clean: 
	rm -f SUBSYS.o $(OBJS) 

ifeq (depend,$(wildcard depend))
include depend
endif

