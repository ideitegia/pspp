#!/bin/sh

# This program tests that the descriptives command actually works

TEMPDIR=/tmp/pspp-tst-$$

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

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
cat > $TEMPDIR/descript.stat <<EOF
title 'Test DESCRIPTIVES procedure'.

data list / V0 to V16 1-17.
begin data.
12128989012389023
34128080123890128
56127781237893217
78127378123793112
90913781237892318
37978547878935789
52878237892378279
12377912789378932
26787654347894348
29137178947891888
end data.

descript all/stat=all/format=serial.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/descript.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - <<EOF
Title: Test DESCRIPTIVES procedure

Table: Reading 1 record from INLINE.
Variable,Record,Columns,Format
V0,1,1-  1,F1.0
V1,1,2-  2,F1.0
V2,1,3-  3,F1.0
V3,1,4-  4,F1.0
V4,1,5-  5,F1.0
V5,1,6-  6,F1.0
V6,1,7-  7,F1.0
V7,1,8-  8,F1.0
V8,1,9-  9,F1.0
V9,1,10- 10,F1.0
V10,1,11- 11,F1.0
V11,1,12- 12,F1.0
V12,1,13- 13,F1.0
V13,1,14- 14,F1.0
V14,1,15- 15,F1.0
V15,1,16- 16,F1.0
V16,1,17- 17,F1.0

Table: Valid cases = 10; cases with missing value(s) = 0.
Variable,Valid N,Missing N,Mean,S.E. Mean,Std Dev,Variance,Kurtosis,S.E. Kurt,Skewness,S.E. Skew,Range,Minimum,Maximum,Sum
V0,10,0,3.80,.84,2.66,7.07,-.03,1.33,.89,.69,8.00,1.00,9.00,38.00
V1,10,0,4.60,.96,3.03,9.16,-1.39,1.33,-.03,.69,9.00,.00,9.00,46.00
V2,10,0,4.10,1.16,3.67,13.43,-2.02,1.33,.48,.69,8.00,1.00,9.00,41.00
V3,10,0,4.10,.87,2.77,7.66,-2.05,1.33,.42,.69,7.00,1.00,8.00,41.00
V4,10,0,7.00,.47,1.49,2.22,7.15,1.33,-2.52,.69,5.00,3.00,8.00,70.00
V5,10,0,4.90,1.03,3.25,10.54,-1.40,1.33,-.20,.69,9.00,.00,9.00,49.00
V6,10,0,5.90,.80,2.51,6.32,-.29,1.33,-.96,.69,7.00,1.00,8.00,59.00
V7,10,0,4.70,1.10,3.47,12.01,-1.99,1.33,-.16,.69,9.00,.00,9.00,47.00
V8,10,0,4.10,1.10,3.48,12.10,-1.93,1.33,.37,.69,9.00,.00,9.00,41.00
V9,10,0,4.30,.87,2.75,7.57,-.87,1.33,.73,.69,8.00,1.00,9.00,43.00
V10,10,0,5.50,.85,2.68,7.17,-1.84,1.33,-.33,.69,7.00,2.00,9.00,55.00
V11,10,0,6.50,.78,2.46,6.06,-1.28,1.33,-.89,.69,6.00,3.00,9.00,65.00
V12,10,0,7.90,.60,1.91,3.66,5.24,1.33,-2.21,.69,6.00,3.00,9.00,79.00
V13,10,0,4.30,.99,3.13,9.79,-1.25,1.33,.33,.69,9.00,.00,9.00,43.00
V14,10,0,3.60,1.01,3.20,10.27,-.96,1.33,.81,.69,9.00,.00,9.00,36.00
V15,10,0,3.70,.92,2.91,8.46,-1.35,1.33,.71,.69,7.00,1.00,8.00,37.00
V16,10,0,6.40,.91,2.88,8.27,-1.14,1.33,-.92,.69,7.00,2.00,9.00,64.00
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
