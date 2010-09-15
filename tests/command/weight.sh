#!/bin/sh

# This program tests the WEIGHT command

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
cat > $TESTFILE << EOF
SET FORMAT F8.3.
data list file='$top_srcdir/tests/weighting.data'/AVAR 1-5 BVAR 6-10.
weight by BVAR.

descriptives AVAR /statistics all /format serial.
frequencies AVAR /statistics all.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading 1 record from \`$top_srcdir/tests/weighting.data'.
Variable,Record,Columns,Format
AVAR,1,1-  5,F5.0
BVAR,1,6- 10,F5.0

Table: Valid cases = 730; cases with missing value(s) = 0.
Variable,Valid N,Missing N,Mean,S.E. Mean,Std Dev,Variance,Kurtosis,S.E. Kurt,Skewness,S.E. Skew,Range,Minimum,Maximum,Sum
AVAR,730,0,31.515,.405,10.937,119.608,2.411,.181,1.345,.090,76.000,18.000,94.000,23006.00

Table: AVAR
Value Label,Value,Frequency,Percent,Valid Percent,Cum Percent
,18,1,.137,.137,.137
,19,7,.959,.959,1.096
,20,26,3.562,3.562,4.658
,21,76,10.411,10.411,15.068
,22,57,7.808,7.808,22.877
,23,58,7.945,7.945,30.822
,24,38,5.205,5.205,36.027
,25,38,5.205,5.205,41.233
,26,30,4.110,4.110,45.342
,27,21,2.877,2.877,48.219
,28,23,3.151,3.151,51.370
,29,24,3.288,3.288,54.658
,30,23,3.151,3.151,57.808
,31,14,1.918,1.918,59.726
,32,21,2.877,2.877,62.603
,33,21,2.877,2.877,65.479
,34,14,1.918,1.918,67.397
,35,14,1.918,1.918,69.315
,36,17,2.329,2.329,71.644
,37,11,1.507,1.507,73.151
,38,16,2.192,2.192,75.342
,39,14,1.918,1.918,77.260
,40,15,2.055,2.055,79.315
,41,14,1.918,1.918,81.233
,42,14,1.918,1.918,83.151
,43,8,1.096,1.096,84.247
,44,15,2.055,2.055,86.301
,45,10,1.370,1.370,87.671
,46,12,1.644,1.644,89.315
,47,13,1.781,1.781,91.096
,48,13,1.781,1.781,92.877
,49,5,.685,.685,93.562
,50,5,.685,.685,94.247
,51,3,.411,.411,94.658
,52,7,.959,.959,95.616
,53,6,.822,.822,96.438
,54,2,.274,.274,96.712
,55,2,.274,.274,96.986
,56,2,.274,.274,97.260
,57,3,.411,.411,97.671
,58,1,.137,.137,97.808
,59,3,.411,.411,98.219
,61,1,.137,.137,98.356
,62,3,.411,.411,98.767
,63,1,.137,.137,98.904
,64,1,.137,.137,99.041
,65,2,.274,.274,99.315
,70,1,.137,.137,99.452
,78,1,.137,.137,99.589
,79,1,.137,.137,99.726
,80,1,.137,.137,99.863
,94,1,.137,.137,100.000
Total,,730,100.0,100.0,

Table: AVAR
N,Valid,730
,Missing,0
Mean,,31.515
S.E. Mean,,.405
Mode,,21.000
Std Dev,,10.937
Variance,,119.608
Kurtosis,,2.411
S.E. Kurt,,.181
Skewness,,1.345
S.E. Skew,,.090
Range,,76.000
Minimum,,18.000
Maximum,,94.000
Sum,,23006.00
Percentiles,50 (Median),29
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
