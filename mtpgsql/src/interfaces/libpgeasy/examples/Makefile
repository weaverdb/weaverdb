#-------------------------------------------------------------------------
#
# Makefile
#    Makefile for pgeasy examples
#
# IDENTIFICATION
#    $Header: /cvs/weaver/mtpgsql/src/interfaces/libpgeasy/examples/Makefile,v 1.1.1.1 2006/08/12 00:23:00 synmscott Exp $
#
#-------------------------------------------------------------------------

CFLAGS=-I/usr/local/pgsql/include
TARGET = pginsert pgwordcount pgnulltest
LDFLAGS = -L/usr/local/pgsql/lib -lpgeasy

all : $(TARGET)

pginsert:
	gcc -o $@ $(CFLAGS) $@.c $(PGEASY) $(LDFLAGS)

pgwordcount:
	gcc -o $@ $(CFLAGS) $@.c $(PGEASY) $(LDFLAGS)

pgnulltest:
	gcc -o $@ $(CFLAGS) $@.c $(PGEASY) $(LDFLAGS)

clean:
	rm -f *.o $(TARGET) log core

