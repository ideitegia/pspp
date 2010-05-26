#!/bin/sh

# This program tests that the T-TEST /PAIRS command works OK

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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
data list list /ID * A * B *.
begin data.
1 2.0 3.0
2 1.0 2.0
3 2.0 4.5
4 2.0 4.5
5 3.0 6.0
end data.

t-test /PAIRS a with b (PAIRED).
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
ID,F8.0
A,F8.0
B,F8.0

Table: Paired Sample Statistics
,,Mean,N,Std. Deviation,S.E. Mean
Pair 0,A,2.00,5,.71,.32
,B,4.00,5,1.54,.69

Table: Paired Samples Correlations
,,N,Correlation,Sig.
Pair 0,A & B,5,.92,.03

Table: Paired Samples Test
,,Paired Differences,,,,,,,
,,,,,95% Confidence Interval of the Difference,,,,
,,Mean,Std. Deviation,Std. Error Mean,Lower,Upper,t,df,Sig. (2-tailed)
Pair 0,A - B,-2.00,.94,.42,-3.16,-.84,-4.78,4,.01
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
