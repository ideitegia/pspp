#!/bin/sh

# This program tests  the Trimmed Mean calculation, in the case
# where the data is weighted towards the centre

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


activity="create program"
cat > $TESTFILE <<EOF
DATA LIST LIST /X * C *.
BEGIN DATA.
1 1
2 49
3 2
END DATA.

WEIGHT BY c.

EXAMINE
	x
	/STATISTICS=DESCRIPTIVES
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: Reading free-form data from INLINE.
Variable,Format
X,F8.0
C,F8.0

Table: Case Processing Summary
,Cases,,,,,
,Valid,,Missing,,Total,
,N,Percent,N,Percent,N,Percent
X,52.00,100%,.00,0%,52.00,100%

Table: Descriptives
,,,Statistic,Std. Error
X,Mean,,2.02,.03
,95% Confidence Interval for Mean,Lower Bound,1.95,
,,Upper Bound,2.09,
,5% Trimmed Mean,,2.00,
,Median,,2.00,
,Variance,,.06,
,Std. Deviation,,.24,
,Minimum,,1.00,
,Maximum,,3.00,
,Range,,2.00,
,Interquartile Range,,.00,
,Skewness,,1.19,.33
,Kurtosis,,15.73,.65
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
