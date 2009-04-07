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
frequencies AVAR /statistics all /format condense.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
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
+--------#-------+---------+------+--------+-------+--------+--------+--------+--------+--------+------+-------+-------+--------+
|Variable#Valid N|Missing N| Mean |S E Mean|Std Dev|Variance|Kurtosis|S E Kurt|Skewness|S E Skew| Range|Minimum|Maximum|   Sum  |
#========#=======#=========#======#========#=======#========#========#========#========#========#======#=======#=======#========#
|AVAR    #    730|        0|31.515|    .405| 10.937| 119.608|   2.411|    .181|   1.345|    .090|76.000| 18.000| 94.000|23006.00|
+--------#-------+---------+------+--------+-------+--------+--------+--------+--------+--------+------+-------+-------+--------+
3.1 FREQUENCIES.  AVAR
+--------+--------+---+---+
|        |        |   |Cum|
|  Value |  Freq  |Pct|Pct|
#========#========#===#===#
|      18|       1|.13|.13|
|      19|       7|.95|1.0|
|      20|      26|3.5|4.6|
|      21|      76|10.|15.|
|      22|      57|7.8|22.|
|      23|      58|7.9|30.|
|      24|      38|5.2|36.|
|      25|      38|5.2|41.|
|      26|      30|4.1|45.|
|      27|      21|2.8|48.|
|      28|      23|3.1|51.|
|      29|      24|3.2|54.|
|      30|      23|3.1|57.|
|      31|      14|1.9|59.|
|      32|      21|2.8|62.|
|      33|      21|2.8|65.|
|      34|      14|1.9|67.|
|      35|      14|1.9|69.|
|      36|      17|2.3|71.|
|      37|      11|1.5|73.|
|      38|      16|2.1|75.|
|      39|      14|1.9|77.|
|      40|      15|2.0|79.|
|      41|      14|1.9|81.|
|      42|      14|1.9|83.|
|      43|       8|1.0|84.|
|      44|      15|2.0|86.|
|      45|      10|1.3|87.|
|      46|      12|1.6|89.|
|      47|      13|1.7|91.|
|      48|      13|1.7|92.|
|      49|       5|.68|93.|
|      50|       5|.68|94.|
|      51|       3|.41|94.|
|      52|       7|.95|95.|
|      53|       6|.82|96.|
|      54|       2|.27|96.|
|      55|       2|.27|96.|
|      56|       2|.27|97.|
|      57|       3|.41|97.|
|      58|       1|.13|97.|
|      59|       3|.41|98.|
|      61|       1|.13|98.|
|      62|       3|.41|98.|
|      63|       1|.13|98.|
|      64|       1|.13|99.|
|      65|       2|.27|99.|
|      70|       1|.13|99.|
|      78|       1|.13|99.|
|      79|       1|.13|99.|
|      80|       1|.13|99.|
|      94|       1|.13|100|
+--------+--------+---+---+
+-----------------------+--------+
|N           Valid      |     730|
|            Missing    |       0|
|Mean                   |  31.515|
|S.E. Mean              |    .405|
|Mode                   |  21.000|
|Std Dev                |  10.937|
|Variance               | 119.608|
|Kurtosis               |   2.411|
|S.E. Kurt              |    .181|
|Skewness               |   1.345|
|S.E. Skew              |    .090|
|Range                  |  76.000|
|Minimum                |  18.000|
|Maximum                |  94.000|
|Sum                    |23006.00|
|Percentiles 50 (Median)|      29|
+-----------------------+--------+
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
