#! /bin/sh

# Tests calculation of percentiles with the 
# ENHANCED algorithm set.

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
     cd /
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
DATA LIST LIST notable /X * .
BEGIN DATA.
1 
2 
3 
4 
5
END DATA.

FREQUENCIES 
	VAR=x
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi

activity="run program $i"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF
1.1 FREQUENCIES.  X 
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
+-----------------------+-----+
|N           Valid      |    5|
|            Missing    |    0|
|Mean                   |3.000|
|Std Dev                |1.581|
|Minimum                |1.000|
|Maximum                |5.000|
|Percentiles 0          |1.000|
|            25         |2.000|
|            50 (Median)|3.000|
|            75         |4.000|
|            100        |5.000|
+-----------------------+-----+
EOF
if [ $? -ne 0 ] ; then fail ; fi



i=$[$i+1];

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /X * F *.
BEGIN DATA.
1 2
2 2
3 2
4 1
4 1
5 1
5 1
END DATA.

WEIGHT BY f.

FREQUENCIES 
	VAR=x
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi


activity="run program $i"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF
1.1 FREQUENCIES.  X 
+-----------+--------+---------+--------+--------+--------+
|           |        |         |        |  Valid |   Cum  |
|Value Label|  Value |Frequency| Percent| Percent| Percent|
#===========#========#=========#========#========#========#
|           |    1.00|        2|    20.0|    20.0|    20.0|
|           |    2.00|        2|    20.0|    20.0|    40.0|
|           |    3.00|        2|    20.0|    20.0|    60.0|
|           |    4.00|        2|    20.0|    20.0|    80.0|
|           |    5.00|        2|    20.0|    20.0|   100.0|
#===========#========#=========#========#========#========#
|               Total|       10|   100.0|   100.0|        |
+--------------------+---------+--------+--------+--------+
+-----------------------+-----+
|N           Valid      |   10|
|            Missing    |    0|
|Mean                   |3.000|
|Std Dev                |1.491|
|Minimum                |1.000|
|Maximum                |5.000|
|Percentiles 0          |1.000|
|            25         |2.000|
|            50 (Median)|3.000|
|            75         |4.000|
|            100        |5.000|
+-----------------------+-----+
EOF
if [ $? -ne 0 ] ; then fail ; fi



i=$[$i+1];

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /X * F *.
BEGIN DATA.
1 1
3 2
4 1
5 1
5 1
END DATA.

WEIGHT BY f.

FREQUENCIES 
	VAR=x
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi


activity="run program $i"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF
1.1 FREQUENCIES.  X 
+-----------+--------+---------+--------+--------+--------+
|           |        |         |        |  Valid |   Cum  |
|Value Label|  Value |Frequency| Percent| Percent| Percent|
#===========#========#=========#========#========#========#
|           |    1.00|        1|    16.7|    16.7|    16.7|
|           |    3.00|        2|    33.3|    33.3|    50.0|
|           |    4.00|        1|    16.7|    16.7|    66.7|
|           |    5.00|        2|    33.3|    33.3|   100.0|
#===========#========#=========#========#========#========#
|               Total|        6|   100.0|   100.0|        |
+--------------------+---------+--------+--------+--------+
+-----------------------+-----+
|N           Valid      |    6|
|            Missing    |    0|
|Mean                   |3.500|
|Std Dev                |1.517|
|Minimum                |1.000|
|Maximum                |5.000|
|Percentiles 0          |1.000|
|            25         |3.000|
|            50 (Median)|3.500|
|            75         |4.750|
|            100        |5.000|
+-----------------------+-----+
EOF
if [ $? -ne 0 ] ; then fail ; fi

i=$[$i+1];

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /X * F *.
BEGIN DATA.
1 1
3 2
4 1
5 1
5 1
99 4
END DATA.

MISSING VALUE x (99.0) .
WEIGHT BY f.

FREQUENCIES 
	VAR=x
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi


activity="run program $i"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b $TEMPDIR/pspp.list - <<EOF
1.1 FREQUENCIES.  X 
+-----------+--------+---------+--------+--------+--------+
|           |        |         |        |  Valid |   Cum  |
|Value Label|  Value |Frequency| Percent| Percent| Percent|
#===========#========#=========#========#========#========#
|           |    1.00|        1|    10.0|    16.7|    16.7|
|           |    3.00|        2|    20.0|    33.3|    50.0|
|           |    4.00|        1|    10.0|    16.7|    66.7|
|           |    5.00|        2|    20.0|    33.3|   100.0|
|           |   99.00|        4|    40.0| Missing|        |
#===========#========#=========#========#========#========#
|               Total|       10|   100.0|   100.0|        |
+--------------------+---------+--------+--------+--------+
+-----------------------+-----+
|N           Valid      |    6|
|            Missing    |    4|
|Mean                   |3.500|
|Std Dev                |1.517|
|Minimum                |1.000|
|Maximum                |5.000|
|Percentiles 0          |1.000|
|            25         |3.000|
|            50 (Median)|3.500|
|            75         |4.750|
|            100        |5.000|
+-----------------------+-----+
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
