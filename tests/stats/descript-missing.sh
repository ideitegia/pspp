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

data list / V1 TO V3 1-3.
mis val v1 to v3 (1).
begin data.
111
   
 1 
1 1
112
123
234
end data.

descript all/stat=all/format=serial.
descript all/stat=all/format=serial/missing=include.
descript all/stat=all/format=serial/missing=listwise.
descript all/stat=all/format=serial/missing=listwise include.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/descript.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from INLINE.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|V1      |     1|  1-  1|F1.0  |
|V2      |     1|  2-  2|F1.0  |
|V3      |     1|  3-  3|F1.0  |
+--------+------+-------+------+
2.1 DESCRIPTIVES.  Valid cases = 7; cases with missing value(s) = 6.
+--------#-------+---------+----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+----+
|Variable#Valid N|Missing N|Mean|S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew|Range|Minimum|Maximum| Sum|
#========#=======#=========#====#========#=======#========#========#========#========#========#=====#=======#=======#====#
|V1      #      1|        6|2.00|     .  |    .  |     .  |     .  |     .  |     .  |     .  |  .00|   2.00|   2.00|2.00|
|V2      #      2|        5|2.50|     .50|    .71|     .50|     .  |     .  |     .  |     .  | 1.00|   2.00|   3.00|5.00|
|V3      #      3|        4|3.00|     .58|   1.00|    1.00|     .  |     .  |     .00|    1.22| 2.00|   2.00|   4.00|9.00|
+--------#-------+---------+----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+----+
3.1 DESCRIPTIVES.  Valid cases = 7; cases with missing value(s) = 3.
+--------#-------+---------+----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+-----+
|Variable#Valid N|Missing N|Mean|S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew|Range|Minimum|Maximum| Sum |
#========#=======#=========#====#========#=======#========#========#========#========#========#=====#=======#=======#=====#
|V1      #      5|        2|1.20|     .20|    .45|     .20|    5.00|    2.00|    2.24|     .91| 1.00|   1.00|   2.00| 6.00|
|V2      #      5|        2|1.60|     .40|    .89|     .80|     .31|    2.00|    1.26|     .91| 2.00|   1.00|   3.00| 8.00|
|V3      #      5|        2|2.20|     .58|   1.30|    1.70|   -1.49|    2.00|     .54|     .91| 3.00|   1.00|   4.00|11.00|
+--------#-------+---------+----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+-----+
4.1 DESCRIPTIVES.  Valid cases = 1; cases with missing value(s) = 6.
+--------#-------+---------+----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+----+
|Variable#Valid N|Missing N|Mean|S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew|Range|Minimum|Maximum| Sum|
#========#=======#=========#====#========#=======#========#========#========#========#========#=====#=======#=======#====#
|V1      #      1|        0|2.00|     .  |    .  |     .  |     .  |     .  |     .  |     .  |  .00|   2.00|   2.00|2.00|
|V2      #      1|        0|3.00|     .  |    .  |     .  |     .  |     .  |     .  |     .  |  .00|   3.00|   3.00|3.00|
|V3      #      1|        0|4.00|     .  |    .  |     .  |     .  |     .  |     .  |     .  |  .00|   4.00|   4.00|4.00|
+--------#-------+---------+----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+----+
5.1 DESCRIPTIVES.  Valid cases = 4; cases with missing value(s) = 3.
+--------#-------+---------+----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+-----+
|Variable#Valid N|Missing N|Mean|S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew|Range|Minimum|Maximum| Sum |
#========#=======#=========#====#========#=======#========#========#========#========#========#=====#=======#=======#=====#
|V1      #      4|        0|1.25|     .25|    .50|     .25|    4.00|    2.62|    2.00|    1.01| 1.00|   1.00|   2.00| 5.00|
|V2      #      4|        0|1.75|     .48|    .96|     .92|   -1.29|    2.62|     .85|    1.01| 2.00|   1.00|   3.00| 7.00|
|V3      #      4|        0|2.50|     .65|   1.29|    1.67|   -1.20|    2.62|     .00|    1.01| 3.00|   1.00|   4.00|10.00|
+--------#-------+---------+----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+-----+
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
