#!/bin/sh

# This program tests for a bug which caused FREQUENCIES following
# TEMPORARY to crash (PR 11492).

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

$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  -w $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from INLINE.
+--------+------+
|Variable|Format|
#========#======#
|SEX     |A1    |
|X       |F8.0  |
+--------+------+
2.1 FREQUENCIES.  X 
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
