#!/bin/sh

# This program tests for a bug which crashed pspp when doing a crosstabs

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

activity="create program"
cat > $TEMPDIR/ct.stat <<EOF
DATA LIST FIXED
     / x   1-2
       y   3
       z   4.

BEGIN DATA.
0111 
0222 
0311 
0412 
0521 
0612 
0711 
0811 
0912 
END DATA.

LIST.


CROSSTABS TABLES  y by z.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/ct.stat
if [ $? -ne 0 ] ; then fail ; fi


pass;
