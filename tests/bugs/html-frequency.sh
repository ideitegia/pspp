#!/bin/sh

# This program tests for a bug where pspp would crash 
# when a FREQUENCIES command was used with the html 
# driver.


TEMPDIR=/tmp/pspp-tst-$$

here=`pwd`

# ensure that top_srcdir is absolute
cd $top_srcdir ; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config

cleanup()
{
     rm -rf $TEMPDIR
}


fail()
{
    echo $activity
    echo FAILED
    cleanup;
    exit 1;
}


no_result()
{
    echo $activity
    echo NO RESULT;
    cleanup;
    exit 2;
}

pass()
{
    cleanup;
    exit 0;
}

mkdir -p $TEMPDIR


activity="create data"
cat << EOF > $TEMPDIR/ff.stat 

data list free /v1 v2.
begin data.
0 1
2 3 
4 5
3 4
end data.

list.

frequencies v1 v2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

cd $TEMPDIR

activity="run data"
$SUPERVISOR $here/../src/pspp -o html $TEMPDIR/ff.stat
if [ $? -ne 0 ] ; then fail ; fi


pass;
