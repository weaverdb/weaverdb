#!/bin/sh
PGUSER=`logname`
cd `dirname $0`/../..
if [ ! -n $MTPG ]
then
    MTPG=`dirname $0`/..;
fi

echo "reindex database $1 force" | $MTPG/bin/postgres -P -D`pwd`/dbfiles $1
