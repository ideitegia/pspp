#!/bin/sh

# This program tests variable display attribute commands: VARIABLE
# ALIGNMENT, VARIABLE WIDTH, VARIABLE LEVEL.

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

# Create command file.
activity="create program"
cat > $TESTFILE << EOF
data list free /x y z.
variable alignment x (left)/y (right)/z (center).
variable width x (10)/y (12)/z (14).
variable level x (scale)/y (ordinal)/z (nominal).
display dictionary.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - << EOF
Variable,Description,,Position
x,Format: F8.2,,1
,Measure: Scale,,
,Display Alignment: Left,,
,Display Width: 10,,
y,Format: F8.2,,2
,Measure: Ordinal,,
,Display Alignment: Right,,
,Display Width: 12,,
z,Format: F8.2,,3
,Measure: Nominal,,
,Display Alignment: Center,,
,Display Width: 14,,
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
