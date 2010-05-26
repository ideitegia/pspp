#!/bin/sh

# This program tests that signals are properly caught and handled by PSPP

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`

PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH

LANG=C
export LANG


cleanup()
{
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR"
     	return ; 
     fi
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

cd $TEMPDIR

activity="sending SIGINT to pspp"
echo 'host kill -INT $PPID' | $PSPP -o pspp.csv > /dev/null 2> $TEMPDIR/stderr1
if [ $? -ne 0 ] ; then no_result ; fi

# SIGINT should have caused a clean shutdown
activity="checking for absence of error messages 1"
[ ! -s $TEMPDIR/stderr1 ]  
if [ $? -ne 0 ] ; then fail ; fi

activity="sending SIGSEGV to pspp"
echo 'host kill -SEGV $PPID' | $PSPP -o pspp.csv > /dev/null 2> $TEMPDIR/stderr2
if [ $? -eq 0 ] ; then no_result ; fi

# SIGSEGV should have caused an error message
activity="checking for error messages from pspp 2"
head -8 $TEMPDIR/stderr2 > $TEMPDIR/stderr-head
if [ $? -ne 0 ] ; then no_result ; fi

activity="comparing error messages from pspp 2"
diff $TEMPDIR/stderr-head  - << EOF
******************************************************
You have discovered a bug in PSPP.  Please report this
to bug-gnu-pspp@gnu.org.  Please include this entire
message, *plus* several lines of output just above it.
For the best chance at having the bug fixed, also
include the syntax file that triggered it and a sample
of any data file used for input.
proximate cause:     Segmentation Violation
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
