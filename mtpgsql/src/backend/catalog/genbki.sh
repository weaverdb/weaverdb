#!/bin/sh
#-------------------------------------------------------------------------
#
# genbki.sh--
#    shell script which generates .bki files from specially formatted .h
#    files.  These .bki files are used to initialize the postgres template
#    database.
#
# Copyright (c) 1994, Regents of the University of California
#
#
#
# NOTES
#    non-essential whitespace is removed from the generated file.
#    if this is ever a problem, then the sed script at the very
#    end can be changed into another awk script or something smarter..
#
#-------------------------------------------------------------------------
#trap "rm -f /tmp/genbki.tmp /tmp/genbkitmp.c" 0 1 2 3 15

# make sure it is empty
>/tmp/genbki.tmp

if [ $? != 0 ]
then
    echo `basename $0`: Bad option
    exit 1
fi

for opt in $*
do
    case $opt in
    -D) BKIOPTS="$BKIOPTS -D$2"; shift; shift;;
    -D*) BKIOPTS="$BKIOPTS $1";shift;;
    --) BKIOPTS="$BKIOPTS $1";shift;;
    -*) BKIOPTS="$BKIOPTS $1";shift;;
    esac
done
# ----------------
# 	collect nodefiles
# ----------------
SYSFILES=''
x=1
numargs=$#
while test $x -le $numargs ; do
    SYSFILES="$SYSFILES $1"
    x=`expr $x + 1`
    shift
done

# Get NAMEDATALEN from postgres_ext.h
NAMEDATALEN=`grep '#define[ 	]*NAMEDATALEN' ../../include/postgres_ext.h | awk '{ print $3 }'`
# Get OIDSIZE from postgres_ext.h
#OIDSIZE=`grep '#define[ 	]*OIDSIZE' ../../include/postgres_ext.h | awk '{ print $3 }'`
# Get LONGSIZE from postgres_ext.h

#LONGSIZE=`grep '#define[ 	]*LONGSIZE' ${CONFIGFILE}| awk '{ print $3 }'`

# Get INDEX_MAX_KEYS from config.h (who needs consistency?)
INDEXMAXKEYS=`grep '#define[ 	]*INDEX_MAX_KEYS' ${CONFIGFILE} | awk '{ print $3 }'`

# NOTE: we assume here that FUNC_MAX_ARGS has the same value as INDEX_MAX_KEYS,
# and don't read it separately from config.h.  This is OK because both of them
# must be equal to the length of oidvector.

INDEXMAXKEYS2=`expr $INDEXMAXKEYS '*' 2`
INDEXMAXKEYS4=`expr $INDEXMAXKEYS '*' 4`
#  INDEXMAXKEYSOIDSIZE=`expr $INDEXMAXKEYS '*' $OIDSIZE`

# ----------------
# 	strip comments and trash from .h before we generate
#	the .bki file...
# ----------------
#	also, change Oid to oid. -- AY 8/94.
#	also, change NameData to name. -- jolly 8/21/95.
#	put multi-line start/end comments on a separate line
#
cat $SYSFILES | ${CPP} -E ${BKIOPTS} - > /tmp/stagebki.c;

cat /tmp/stagebki.c | \
sed -e 's;/\*.*\*/;;g' \
    -e 's;/\*;\
/*\
;g' \
    -e 's;\*/;\
*/\
;g' | # we must run a new sed here to see the newlines we added
#    -e "s/INDEX_MAX_KEYS\*$OIDSIZE/$INDEXMAXKEYSOIDSIZE/g" \
#    -e "s/OIDSIZE/$OIDSIZE/g" \
#    -e "s/LONGSIZE/$LONGSIZE/g" \

sed -e "s/^#.*//g" \
    -e "s/;[ 	]*$//g" \
    -e "s/^[ 	]*//" \
    -e "s/[ 	]Oid/\ oid/g" \
    -e "s/[ 	]NameData/\ name/g" \
    -e "s/^Oid/oid/g" \
    -e "s/^NameData/\ name/g" \
    -e "s/(NameData/(name/g" \
    -e "s/(Oid/(oid/g" \
    -e "s/NAMEDATALEN/$NAMEDATALEN/g" \
    -e "s/INDEX_MAX_KEYS\*2/$INDEXMAXKEYS2/g" \
    -e "s/INDEX_MAX_KEYS\*4/$INDEXMAXKEYS4/g" \
    -e "s/INDEX_MAX_KEYS/$INDEXMAXKEYS/g" \
    -e "s/FUNC_MAX_ARGS\*2/$INDEXMAXKEYS2/g" \
    -e "s/FUNC_MAX_ARGS\*4/$INDEXMAXKEYS4/g" \
    -e "s/FUNC_MAX_ARGS/$INDEXMAXKEYS/g" \
| awk '
# ----------------
#	now use awk to process remaining .h file..
#
#	nc is the number of catalogs
#	inside is a variable set to 1 when we are scanning the
#	   contents of a catalog definition.
#	inserting_data is a flag indicating when we are processing DATA lines.
#		(i.e. have a relation open and need to close it)
# ----------------
BEGIN {
	inside = 0;
	raw = 0;
	bootstrap = 0;
	nc = 0;
	reln_open = 0;
        comment_level = 0;
}

# ----------------
# Anything in a /* .. */ block should be ignored.
# Blank lines also go.
# Note that any /* */ comment on a line by itself was removed from the line
# by the sed above.
# ----------------
/^\/\*/           { comment_level += 1; next; }
/^\*\//           { comment_level -= 1; next; }
comment_level > 0 { next; }

/^[ 	]*$/      { next; }

# ----------------
#	anything in a BKI_BEGIN .. BKI_END block should be passed
#	along without interpretation.
# ----------------
/^BKI_BEGIN/ 	{ raw = 1; next; }
/^BKI_END/ 	{ raw = 0; next; }
raw == 1 	{ print; next; }

# ----------------
#	DATA() statements should get passed right through after
#	stripping off the DATA( and the ) on the end.
# ----------------
/^DATA\(/ {
	data = substr($0, 6, length($0) - 6);
	print data;
	nf = 1;
	oid = 0;
	while (nf <= NF-3)
	{
		if ($nf == "OID" && $(nf+1) == "=")
		{
			oid = $(nf+2);
			break;
		}
		nf++;
	}
	next;
}

/^DESCR\(/ {
	if (oid != 0)
	{
		data = substr($0, 8, length($0) - 9);
		if (data != "")
			printf "%d	%s\n", oid, data >> "/tmp/genbki.tmp";
	}
	next;
}

/^DECLARE_INDEX\(/ {
# ----
#  end any prior catalog data insertions before starting a define index
# ----
	if (reln_open == 1) {
#		print "show";
		print "close " catalog;
		reln_open = 0;
	}

	data = substr($0, 15, length($0) - 15);
	print "declare index " data
}

/^DECLARE_UNIQUE_INDEX\(/ {
# ----
#  end any prior catalog data insertions before starting a define unique index
# ----
	if (reln_open == 1) {
#		print "show";
		print "close " catalog;
		reln_open = 0;
	}

	data = substr($0, 22, length($0) - 22);
	print "declare unique index " data
}

/^BUILD_INDICES/	{ print "build indices"; }
	
# ----------------
#	CATALOG() definitions take some more work.
# ----------------
/^CATALOG\(/ { 
# ----
#  end any prior catalog data insertions before starting a new one..
# ----
	if (reln_open == 1) {
#		print "show";
		print "close " catalog;
		reln_open = 0;
	}

# ----
#  get the name of the new catalog
# ----
	pos = index($1,")");
	catalog = substr($1,9,pos-9); 

	if ($0 ~ /BOOTSTRAP/) {
		bootstrap = 1;
	}

        i = 1;
	inside = 1;
        nc++;
	next;
}

# ----------------
#	process the contents of the catalog definition
#
#	attname[ x ] contains the attribute name for attribute x
#	atttype[ x ] contains the attribute type fot attribute x
# ----------------
inside == 1 {
# ----
#  ignore a leading brace line..
# ----
        if ($1 ~ /\{/)
		next;

# ----
#  if this is the last line, then output the bki catalog stuff.
# ----
	if ($1 ~ /}/) {
		if (bootstrap) {
			print "create bootstrap " catalog;
		} else {
			print "create " catalog;
		}
		print "\t(";

		for (j=1; j<i-1; j++) {
			print "\t " attname[ j ] " = " atttype[ j ] " ,";
		}
		print "\t " attname[ j ] " = " atttype[ j ] ;
		print "\t)";

		if (! bootstrap) {
			print "open " catalog;
		}

		i = 1;
		reln_open = 1;
		inside = 0;
		bootstrap = 0;
		next;
	}

# ----
#  if we are inside the catalog definition, then keep sucking up
#  attibute names and types
# ----
	if ($2 ~ /\[.*\]/) {			# array attribute
		idlen = index($2,"[") - 1;
		atttype[ i ] = $1 "[]";		# variable-length only..
		attname[ i ] = substr($2,1,idlen);
	} else {
		atttype[ i ] = $1;
		attname[ i ] = $2;
	}
	i++;
	next;
}

END {
	if (reln_open == 1) {
#		print "show";
		print "close " catalog;
		reln_open = 0;
	}
}
' >/tmp/genbkitmp.c
#${CPP} -E $BKIOPTS /tmp/genbkitmp.c | \
cat /tmp/genbkitmp.c | \
sed -e '/^[ 	]*$/d' \
    -e 's/[ 	][ 	]*/ /g' || exit 1

# send pg_description file contents to standard error
cat /tmp/genbki.tmp 1>&2

# ----------------
#	all done
# ----------------
exit 0
