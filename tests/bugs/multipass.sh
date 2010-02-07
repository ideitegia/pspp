#!/bin/sh

# This program tests for a bug which caused UNIFORM(x) to always return zero.


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
cat > $TESTFILE <<EOF
data list list /ID * ABC *.
begin data.
1 3.5
2 2.0
3 2.0
4 3.5
5 3.0
6 4.0
7 5.0
end data.

TEMPORARY.
SELECT IF id < 7 .

DESCRIPTIVES
        /VAR=abc.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


diff -c $TEMPDIR/pspp.csv - << EOF
Table: Reading free-form data from INLINE.
Variable,Format
ID,F8.0
ABC,F8.0

Table: Valid cases = 6; cases with missing value(s) = 0.
Variable,N,Mean,Std Dev,Minimum,Maximum
ABC,6,3.00,.84,2.00,4.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
