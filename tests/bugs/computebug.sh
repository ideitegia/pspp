#!/bin/sh

# This program tests for a bug in the `compute' command

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


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

activity="copy file"
cp $top_srcdir/tests/bugs/computebug.stat $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi

activity="chdir"
cd $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/computebug.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
diff -b -B -w $TEMPDIR/pspp.list $top_srcdir/tests/bugs/computebug.out
if [ $? -ne 0 ] ; then fail ; fi

pass;
