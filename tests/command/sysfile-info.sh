#!/bin/sh

# This program tests that SYSFILE INFO works.

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

activity="Create test file"
cat > $TESTFILE << EOF
DATA LIST LIST /x * name (a10) .
BEGIN DATA
1 one
2 two
3 three
END DATA.
SAVE OUTFILE='pro.sav'.

sysfile info file='pro.sav'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="filter output"
egrep -v '^(Created|Endian|Integer Format|Real Format):,' $TEMPDIR/pspp.csv > $TEMPDIR/out-filtered
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -c $TEMPDIR/out-filtered - << EOF
Table: Reading free-form data from INLINE.
Variable,Format
x,F8.0
name,A10

File:,pro.sav
Label:,No label.
Variables:,2
Cases:,3
Type:,System File
Weight:,Not weighted.
Mode:,Compression on.
Charset:,Unknown

Variable,Description,,Position
x,Format: F8.2,,1
,Measure: Scale,,
,Display Alignment: Right,,
,Display Width: 8,,
name,Format: A10,,2
,Measure: Nominal,,
,Display Alignment: Left,,
,Display Width: 10,,
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
