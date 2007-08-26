#!/bin/sh

# This program tests for a bug where pspp would crash if two frequencies
# Commands existed in a input file

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`


STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
     cd /
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

frequencies v1 v2.
frequencies v1 v2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


cd $TEMPDIR

activity="run data"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/ff.stat
if [ $? -ne 0 ] ; then fail ; fi


pass;
