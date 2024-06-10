#!/bin/sh
RETVAL=0;
COUNT=0;
LIMIT=-1;
if [ -n "$2" ]; then 
	echo "Limiting to $2"
	LIMIT=$2; fi;
while [ $RETVAL -eq 0 -a $COUNT -ne $LIMIT ]
do
        ((COUNT=COUNT + 1));
	printf "TEST RUN %s\n" $COUNT;
	eval $1
	RETVAL=$?
done
if [ $RETVAL -eq 0 ]
then
	printf "COMPLETED %s\n" $COUNT
else
	printf "FAILED ON %s\n" $COUNT
fi
