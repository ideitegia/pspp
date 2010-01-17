#!/bin/sh

# This program tests that the T-TEST /TESTVAL command works OK

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
end data.

t-test /testval=2.0 /var=abc.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
ID,F8.0
ABC,F8.0

Table: One-Sample Statistics
,N,Mean,Std. Deviation,S.E. Mean
ABC,6,3.00,.84,.34

Table: One-Sample Test
,Test Value = 2.000000,,,,,
,,,,,95% Confidence Interval of the Difference,
,t,df,Sig. (2-tailed),Mean Difference,Lower,Upper
ABC,2.93,5,.03,1.00,.12,1.88
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
