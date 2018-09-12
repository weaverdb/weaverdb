#!/bin/sh
if [ ! -n $MTPG ]
then
    MTPG=`dirname $0`/..;
fi

DBID=`cat $1/LOCK`;
if kill $DBID ;
then
while [ `$MTPG/bin/comcheck | grep -c ' '*$DBID.*weaver` = "1" ]
do
	sleep 3;
done
fi
