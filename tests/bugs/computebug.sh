#!/bin/sh

# This program tests for a bug in the `compute' command

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`

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

activity="copy file"
cp $top_srcdir/tests/bugs/computebug.stat $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi

activity="chdir"
cd $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $top_builddir/src/pspp -o raw-ascii $TEMPDIR/computebug.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list $top_srcdir/tests/bugs/computebug.out
diff -b -w $TEMPDIR/pspp.list $top_srcdir/tests/bugs/computebug.out
if [ $? -ne 0 ] ; then fail ; fi

pass;
