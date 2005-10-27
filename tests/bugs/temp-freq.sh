#!/bin/sh

# This program tests for a bug which caused FREQUENCIES following
# TEMPORARY to crash (PR 11492).

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
DATA LIST LIST /SEX (A1) X *.
BEGIN DATA.
M 31
F 21
M 41
F 31
M 13
F 12
M 14
F 13
END DATA.


TEMPORARY
SELECT IF SEX EQ 'F'
FREQUENCIES /X .

FINISH
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  -w $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|SEX     |A1    |
|X       |F8.0  |
+--------+------+
2.1 FREQUENCIES.  X: 
+-----------+--------+---------+--------+--------+--------+
|           |        |         |        |  Valid |   Cum  |
|Value Label|  Value |Frequency| Percent| Percent| Percent|
#===========#========#=========#========#========#========#
|           |   12.00|        1|    25.0|    25.0|    25.0|
|           |   13.00|        1|    25.0|    25.0|    50.0|
|           |   21.00|        1|    25.0|    25.0|    75.0|
|           |   31.00|        1|    25.0|    25.0|   100.0|
#===========#========#=========#========#========#========#
|               Total|        4|   100.0|   100.0|        |
+--------------------+---------+--------+--------+--------+
+---------------+------+
|N       Valid  |     4|
|        Missing|     0|
|Mean           |19.250|
|Std Dev        | 8.808|
|Minimum        |12.000|
|Maximum        |31.000|
+---------------+------+
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
