#!/bin/sh
PGUSER=`logname`
cd `dirname $0`/../..
if [ ! -n $MTPG ]
then
    MTPG=`dirname $0`/..;
fi

$MTPG/bin/initdb --pglib=$MTPG/mtpg/lib --pgdata=$1
echo "revoke all on pg_shadow from public;" | $MTPG/bin/postgres -D$1 template1
echo "alter user $PGUSER with password 'manager'" | $MTPG/bin/postgres -D$1 template1
java -classpath $MTPG/server/base_server.jar com.myosyn.server.GenerateUUID dbfiles/uuid.txt
