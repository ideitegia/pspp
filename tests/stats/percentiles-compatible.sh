#! /bin/sh

# Tests calculation of percentiles with the 
# COMPATIBLE algorithm set.

TEMPDIR=/tmp/pspp-tst-$$

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
     rm -rf $TEMPDIR
     :
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


i=1;

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /x * .
BEGIN DATA.
1 
2 
3 
4 
5
END DATA.

FREQUENCIES 
	VAR=x
	/ALGORITHM=COMPATIBLE
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi

activity="run program $i"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
diff -B -b $TEMPDIR/pspp.list - <<EOF
1.1 FREQUENCIES.  X: 
+-----------+--------+---------+--------+--------+--------+
|           |        |         |        |  Valid |   Cum  |
|Value Label|  Value |Frequency| Percent| Percent| Percent|
#===========#========#=========#========#========#========#
|           |    1.00|        1|    20.0|    20.0|    20.0|
|           |    2.00|        1|    20.0|    20.0|    40.0|
|           |    3.00|        1|    20.0|    20.0|    60.0|
|           |    4.00|        1|    20.0|    20.0|    80.0|
|           |    5.00|        1|    20.0|    20.0|   100.0|
#===========#========#=========#========#========#========#
|               Total|        5|   100.0|   100.0|        |
+--------------------+---------+--------+--------+--------+

+-------------------+-----+
|N           Valid  |    5|
|            Missing|    0|
|Mean               |3.000|
|Std Dev            |1.581|
|Minimum            |1.000|
|Maximum            |5.000|
|Percentiles 0      |1.000|
|            25     |1.500|
|            50     |3.000|
|            75     |4.500|
|            100    |5.000|
+-------------------+-----+
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
