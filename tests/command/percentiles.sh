#!/bin/sh

# This program tests the PERCENTILES subcommand of the FREQUENCIES cmd

TEMPDIR=/tmp/pspp-tst-$$

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
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

activity="create prog"
cat > $TEMPDIR/percents.stat <<EOF
data list free /z x(f3.2) y(f3.0) d(a30).
begin data.
1 3 4 apples
2 5 6 pairs
3 4 5 bannanas
4 3 9 pairs
5 1 2 pairs
6 4 5 apricots
7 4 4 bannanas
8 4 5 apples
9 3 3 peaches
10 2 3 coconuts
end data.

frequencies z 	/statistics=all /percentiles = 5,10,30,90.

finish.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run prog"
$here/../src/pspp -o raw-ascii $TEMPDIR/percents.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -b -B $TEMPDIR/pspp.list - <<EOF
1.1 FREQUENCIES.  Z: 
+-----------+--------+---------+--------+--------+--------+
|           |        |         |        |  Valid |   Cum  |
|Value Label|  Value |Frequency| Percent| Percent| Percent|
#===========#========#=========#========#========#========#
|           |     .01|        1|    10.0|    10.0|    10.0|
|           |     .02|        1|    10.0|    10.0|    20.0|
|           |     .03|        1|    10.0|    10.0|    30.0|
|           |     .04|        1|    10.0|    10.0|    40.0|
|           |     .05|        1|    10.0|    10.0|    50.0|
|           |     .06|        1|    10.0|    10.0|    60.0|
|           |     .07|        1|    10.0|    10.0|    70.0|
|           |     .08|        1|    10.0|    10.0|    80.0|
|           |     .09|        1|    10.0|    10.0|    90.0|
|           |     .10|        1|    10.0|    10.0|   100.0|
#===========#========#=========#========#========#========#
|               Total|       10|   100.0|   100.0|        |
+--------------------+---------+--------+--------+--------+

Mean            .055
S.E. Mean       .010
Median          .   
Mode            .   
Std Dev         .030
Variance        .001
Kurtosis      -1.200
S.E. Kurt      1.334
Skewness        .000
S.E. Skew       .687
Range           .090
Minimum         .010
Maximum         .100
Sum             .550
Percentile 5    .   
Percentile 10   .010
Percentile 29   .030
Percentile 90   .090
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
