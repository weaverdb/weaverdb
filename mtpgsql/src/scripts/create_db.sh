#!/bin/sh
PGUSER=`logname`
cd `dirname $0`/../..
if [ ! -n $MTPG ]
then
    MTPG=`dirname $0`/..;
fi

echo "create database $2" | $MTPG/bin/postgres -P -D$1 template1
