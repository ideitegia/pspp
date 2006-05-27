#!/bin/sh

# This program tests the WEIGHT command

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

LANG=C
export LANG


cleanup()
{
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then return ; fi
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
data list file='$top_srcdir/tests/weighting.data'/AVAR 1-5 BVAR 6-10.
weight by BVAR.

descriptives AVAR /statistics all /format serial.
frequencies AVAR /statistics all /format condense.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from "$top_srcdir/tests/weighting.data".
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|AVAR    |     1|  1-  5|F5.0  |
|BVAR    |     1|  6- 10|F5.0  |
+--------+------+-------+------+
2.1 DESCRIPTIVES.  Valid cases = 730; cases with missing value(s) = 0.
+--------#-------+---------+------+--------+-------+--------+--------+--------+--------+--------+------+-------+-------+---------+
|Variable#Valid N|Missing N| Mean |S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew| Range|Minimum|Maximum|   Sum   |
#========#=======#=========#======#========#=======#========#========#========#========#========#======#=======#=======#=========#
|AVAR    #    730|        0|31.515|    .405| 10.937| 119.608|   2.411|    .181|   1.345|    .090|76.000| 18.000| 94.000|23006.000|
+--------#-------+---------+------+--------+-------+--------+--------+--------+--------+--------+------+-------+-------+---------+
3.1 FREQUENCIES.  AVAR: 
+--------+--------+---+---+
|        |        |   |Cum|
|  Value |  Freq  |Pct|Pct|
#========#========#===#===#
|      18|       1|  0|  0|
|      19|       7|  1|  1|
|      20|      26|  4|  5|
|      21|      76| 10| 15|
|      22|      57|  8| 23|
|      23|      58|  8| 31|
|      24|      38|  5| 36|
|      25|      38|  5| 41|
|      26|      30|  4| 45|
|      27|      21|  3| 48|
|      28|      23|  3| 51|
|      29|      24|  3| 55|
|      30|      23|  3| 58|
|      31|      14|  2| 60|
|      32|      21|  3| 63|
|      33|      21|  3| 65|
|      34|      14|  2| 67|
|      35|      14|  2| 69|
|      36|      17|  2| 72|
|      37|      11|  2| 73|
|      38|      16|  2| 75|
|      39|      14|  2| 77|
|      40|      15|  2| 79|
|      41|      14|  2| 81|
|      42|      14|  2| 83|
|      43|       8|  1| 84|
|      44|      15|  2| 86|
|      45|      10|  1| 88|
|      46|      12|  2| 89|
|      47|      13|  2| 91|
|      48|      13|  2| 93|
|      49|       5|  1| 94|
|      50|       5|  1| 94|
|      51|       3|  0| 95|
|      52|       7|  1| 96|
|      53|       6|  1| 96|
|      54|       2|  0| 97|
|      55|       2|  0| 97|
|      56|       2|  0| 97|
|      57|       3|  0| 98|
|      58|       1|  0| 98|
|      59|       3|  0| 98|
|      61|       1|  0| 98|
|      62|       3|  0| 99|
|      63|       1|  0| 99|
|      64|       1|  0| 99|
|      65|       2|  0| 99|
|      70|       1|  0| 99|
|      78|       1|  0|100|
|      79|       1|  0|100|
|      80|       1|  0|100|
|      94|       1|  0|100|
+--------+--------+---+---+
+-----------------+---------+
|N         Valid  |      730|
|          Missing|        0|
|Mean             |   31.515|
|S.E. Mean        |     .405|
|Median           |   28.500|
|Mode             |   21.000|
|Std Dev          |   10.937|
|Variance         |  119.608|
|Kurtosis         |    2.411|
|S.E. Kurt        |     .181|
|Skewness         |    1.345|
|S.E. Skew        |     .090|
|Range            |   76.000|
|Minimum          |   18.000|
|Maximum          |   94.000|
|Sum              |23006.000|
+-----------------+---------+
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
