#!/bin/sh
cd `dirname $0`/../..
if [ ! -n $MTPG ]
then
    MTPG=`dirname $0`/..;
fi

$MTPG/bin/stop_db $1;
$MTPG/bin/start_db $1;
