#!/bin/sh

# This program tests  the EXAMINE command.

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
DATA LIST LIST /QUALITY * W * BRAND * .
BEGIN DATA
3  1  1
2  2  1
1  2  1
1  1  1
4  1  1
4  1  1
5  1  2
2  1  2
4  4  2
2  1  2
3  1  2
7  1  3
4  2  3
5  3  3
3  1  3
6  1  3
END DATA

WEIGHT BY w.

VARIABLE LABELS brand   'Manufacturer'.
VARIABLE LABELS quality 'Breaking Strain'.

VALUE LABELS /brand 1 'Aspeger' 2 'Bloggs' 3 'Charlies'.

LIST /FORMAT=NUMBERED.

EXAMINE
	quality BY brand
	/STATISTICS descriptives extreme(3)
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

# NOTE:  In the following data: Only the extreme values have been checked
# The descriptives have been blindly pasted.
activity="compare results"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: Reading free-form data from INLINE.
Variable,Format
QUALITY,F8.0
W,F8.0
BRAND,F8.0

Table: Data List
Case Number,QUALITY,W,BRAND
1,3.00,1.00,1.00
2,2.00,2.00,1.00
3,1.00,2.00,1.00
4,1.00,1.00,1.00
5,4.00,1.00,1.00
6,4.00,1.00,1.00
7,5.00,1.00,2.00
8,2.00,1.00,2.00
9,4.00,4.00,2.00
10,2.00,1.00,2.00
11,3.00,1.00,2.00
12,7.00,1.00,3.00
13,4.00,2.00,3.00
14,5.00,3.00,3.00
15,3.00,1.00,3.00
16,6.00,1.00,3.00

Table: Case Processing Summary
,Cases,,,,,
,Valid,,Missing,,Total,
,N,Percent,N,Percent,N,Percent
Breaking Strain,24.00,100%,.00,0%,24.00,100%

Table: Extreme Values
,,,Case Number,Value
Breaking Strain,Highest,1,12,7.00
,,2,16,6.00
,,3,7,5.00
,Lowest,1,3,1.00
,,2,3,1.00
,,3,4,1.00

Table: Descriptives
,,,Statistic,Std. Error
Breaking Strain,Mean,,3.54,.32
,95% Confidence Interval for Mean,Lower Bound,2.87,
,,Upper Bound,4.21,
,5% Trimmed Mean,,3.50,
,Median,,4.00,
,Variance,,2.52,
,Std. Deviation,,1.59,
,Minimum,,1.00,
,Maximum,,7.00,
,Range,,6.00,
,Interquartile Range,,2.75,
,Skewness,,.06,.47
,Kurtosis,,-.36,.92

Table: Case Processing Summary
,,Cases,,,,,
,,Valid,,Missing,,Total,
,Manufacturer,N,Percent,N,Percent,N,Percent
Breaking Strain,Aspeger,8.00,100%,.00,0%,8.00,100%
,Bloggs,8.00,100%,.00,0%,8.00,100%
,Charlies,8.00,100%,.00,0%,8.00,100%

Table: Extreme Values
,Manufacturer,,,Case Number,Value
Breaking Strain,Aspeger,Highest,1,5,4.00
,,,2,6,4.00
,,,3,1,3.00
,,Lowest,1,3,1.00
,,,2,3,1.00
,,,3,4,1.00
,Bloggs,Highest,1,7,5.00
,,,2,9,4.00
,,,3,9,4.00
,,Lowest,1,8,2.00
,,,2,10,2.00
,,,3,11,3.00
,Charlies,Highest,1,12,7.00
,,,2,16,6.00
,,,3,14,5.00
,,Lowest,1,15,3.00
,,,2,13,4.00
,,,3,13,4.00

Table: Descriptives
,Manufacturer,,,Statistic,Std. Error
Breaking Strain,Aspeger,Mean,,2.25,.45
,,95% Confidence Interval for Mean,Lower Bound,1.18,
,,,Upper Bound,3.32,
,,5% Trimmed Mean,,2.22,
,,Median,,2.00,
,,Variance,,1.64,
,,Std. Deviation,,1.28,
,,Minimum,,1.00,
,,Maximum,,4.00,
,,Range,,3.00,
,,Interquartile Range,,2.75,
,,Skewness,,.47,.75
,,Kurtosis,,-1.55,1.48
,Bloggs,Mean,,3.50,.38
,,95% Confidence Interval for Mean,Lower Bound,2.61,
,,,Upper Bound,4.39,
,,5% Trimmed Mean,,3.50,
,,Median,,4.00,
,,Variance,,1.14,
,,Std. Deviation,,1.07,
,,Minimum,,2.00,
,,Maximum,,5.00,
,,Range,,3.00,
,,Interquartile Range,,1.75,
,,Skewness,,-.47,.75
,,Kurtosis,,-.83,1.48
,Charlies,Mean,,4.88,.44
,,95% Confidence Interval for Mean,Lower Bound,3.83,
,,,Upper Bound,5.92,
,,5% Trimmed Mean,,4.86,
,,Median,,5.00,
,,Variance,,1.55,
,,Std. Deviation,,1.25,
,,Minimum,,3.00,
,,Maximum,,7.00,
,,Range,,4.00,
,,Interquartile Range,,1.75,
,,Skewness,,.30,.75
,,Kurtosis,,.15,1.48
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
