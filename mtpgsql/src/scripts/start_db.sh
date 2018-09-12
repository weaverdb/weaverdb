#!/bin/sh
cd `dirname $0`/../..
if [ ! -n $MTPG ]
then
    MTPG=`dirname $0`/..;
fi
unset DISPLAY;
JAVA_HOME=/usr/jdk/latest;export JAVA_HOME
CLASSPATH=$MTPG/server/lib/basedata.jar:$MTPG/server/base_server.jar:$MTPG/lib/weaver.jar
# this is now handled by individual classloaders
#for N in $MTPG/server/lib/*;do CLASSPATH=${CLASSPATH}:$N;done
export CLASSPATH
LD_LIBRARY_PATH=$JAVA_HOME/jre/lib/sparcv9/server;export LD_LIBRARY_PATH
LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$MTPG/lib;export LD_LIBRARY_PATH
PGDATA=$1;export PGDATA
PATH=$MTPG/bin:$JAVA_HOME/bin:$PATH;export PATH

find $1 \( -name 'pg_sorttemp*' -o -name 'pg_temp*' \) -exec rm {} \;

UMEM_DEBUG=default;export UMEM_DEBUG

weaver_server -XX:ParallelGCThreads=4 -Xmx256m $MTPG/server.properties
