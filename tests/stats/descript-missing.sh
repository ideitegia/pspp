#!/bin/sh

# This program tests that the descriptives command actually works

TEMPDIR=/tmp/pspp-tst-$$

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
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
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/descript.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|V1      |     1|  1-  1|F1.0  |
|V2      |     1|  2-  2|F1.0  |
|V3      |     1|  3-  3|F1.0  |
+--------+------+-------+------+
2.1 DESCRIPTIVES.  Valid cases = 7; cases with missing value(s) = 6.
+--------#-------+---------+-----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+-----+
|Variable#Valid N|Missing N| Mean|S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew|Range|Minimum|Maximum| Sum |
#========#=======#=========#=====#========#=======#========#========#========#========#========#=====#=======#=======#=====#
|V1      #      1|        6|2.000|    .   |   .   |    .   |    .   |    .   |    .   |    .   | .000|  2.000|  2.000|2.000|
|V2      #      2|        5|2.500|    .500|   .707|    .500|    .   |    .   |    .   |    .   |1.000|  2.000|  3.000|5.000|
|V3      #      3|        4|3.000|    .577|  1.000|   1.000|    .   |    .   |    .000|   1.225|2.000|  2.000|  4.000|9.000|
+--------#-------+---------+-----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+-----+
3.1 DESCRIPTIVES.  Valid cases = 7; cases with missing value(s) = 3.
+--------#-------+---------+-----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+------+
|Variable#Valid N|Missing N| Mean|S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew|Range|Minimum|Maximum|  Sum |
#========#=======#=========#=====#========#=======#========#========#========#========#========#=====#=======#=======#======#
|V1      #      5|        2|1.200|    .200|   .447|    .200|   5.000|   2.000|   2.236|    .913|1.000|  1.000|  2.000| 6.000|
|V2      #      5|        2|1.600|    .400|   .894|    .800|    .312|   2.000|   1.258|    .913|2.000|  1.000|  3.000| 8.000|
|V3      #      5|        2|2.200|    .583|  1.304|   1.700|  -1.488|   2.000|    .541|    .913|3.000|  1.000|  4.000|11.000|
+--------#-------+---------+-----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+------+
4.1 DESCRIPTIVES.  Valid cases = 1; cases with missing value(s) = 6.
+--------#-------+---------+-----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+-----+
|Variable#Valid N|Missing N| Mean|S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew|Range|Minimum|Maximum| Sum |
#========#=======#=========#=====#========#=======#========#========#========#========#========#=====#=======#=======#=====#
|V1      #      1|        0|2.000|    .   |   .   |    .   |    .   |    .   |    .   |    .   | .000|  2.000|  2.000|2.000|
|V2      #      1|        0|3.000|    .   |   .   |    .   |    .   |    .   |    .   |    .   | .000|  3.000|  3.000|3.000|
|V3      #      1|        0|4.000|    .   |   .   |    .   |    .   |    .   |    .   |    .   | .000|  4.000|  4.000|4.000|
+--------#-------+---------+-----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+-----+
5.1 DESCRIPTIVES.  Valid cases = 4; cases with missing value(s) = 3.
+--------#-------+---------+-----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+------+
|Variable#Valid N|Missing N| Mean|S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew|Range|Minimum|Maximum|  Sum |
#========#=======#=========#=====#========#=======#========#========#========#========#========#=====#=======#=======#======#
|V1      #      4|        0|1.250|    .250|   .500|    .250|   4.000|   2.619|   2.000|   1.014|1.000|  1.000|  2.000| 5.000|
|V2      #      4|        0|1.750|    .479|   .957|    .917|  -1.289|   2.619|    .855|   1.014|2.000|  1.000|  3.000| 7.000|
|V3      #      4|        0|2.500|    .645|  1.291|   1.667|  -1.200|   2.619|    .000|   1.014|3.000|  1.000|  4.000|10.000|
+--------#-------+---------+-----+--------+-------+--------+--------+--------+--------+--------+-----+-------+-------+------+
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
