#!/bin/sh

# This program tests that PSPP ignores the first line of a PSPP syntax
# file that begins with #!, without issuing an error (bug #26518).

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`

PSPP=$top_builddir/src/ui/terminal/pspp

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

cat > $TESTFILE << EOF
#! $PSPP
DATA LIST LIST NOTABLE /a.
BEGIN DATA.
1
2
END DATA.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv -e /dev/null $TESTFILE 
if [ $? -ne 0 ] ; then fail ; fi


activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.csv
diff -b  -w $TEMPDIR/pspp.csv - << EOF
Table: Data List
a
1.00
2.00
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
