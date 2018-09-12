# Generated automatically from mkMakefile.tcldefs.sh.in by configure.
#! /bin/sh

if [ ! -r  ]; then
    echo " not found"
    echo "I need this file! Please make a symbolic link to this file"
    echo "and start make again."
    exit 1
fi

# Source the file to obtain the correctly expanded variable definitions
. 

# Read the file a second time as an easy way of getting the list of variable
# definitions to output.
cat  |
    egrep '^TCL_|^TK_' |
    sed 's/^\([^=]*\)=.*$/\1/' |
    while read var
    do
	eval echo "\"$var = \$$var\""
    done >Makefile.tcldefs

exit 0
