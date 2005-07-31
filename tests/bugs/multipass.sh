#!/bin/sh

# This program tests for a bug which caused UNIFORM(x) to always return zero.


TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

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
cat > $TESTFILE <<EOF
data list list /ID * ABC *.
begin data.
1 3.5
2 2.0
3 2.0
4 3.5
5 3.0
6 4.0
7 5.0
end data.

TEMPORARY.
SELECT IF id < 7 .

DESCRIPTIVES
        /VAR=abc.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


diff -b -B -w $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|ID      |F8.0  |
|ABC     |F8.0  |
+--------+------+

2.1 DESCRIPTIVES.  Valid cases = 6; cases with missing value(s) = 0.
+--------#-+-----+-------+-------+-------+
|Variable#N| Mean|Std Dev|Minimum|Maximum|
#========#=#=====#=======#=======#=======#
|ABC     #6|3.000|   .837|  2.000|  4.000|
+--------#-+-----+-------+-------+-------+
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
