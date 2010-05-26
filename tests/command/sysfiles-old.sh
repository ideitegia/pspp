#!/bin/sh

# This program tests that system files can be read and written 
# without the long name tables.


TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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

activity="create program"
cat > $TESTFILE <<EOF
DATA LIST LIST NOTABLE / X * variable001 * variable002 * variable003 * .
BEGIN DATA.
    1.00     1.00    1.0     2.00
    1.00     1.00    2.0    30.00
    1.00     2.00    1.0     8.00
    1.00     2.00    2.0    20.00
    2.00     1.00    1.0     2.00
    2.00     1.00    2.0    22.00
    2.00     2.00    1.0     1.00
    2.00     2.00    2.0     3.00
END DATA.

SAVE /OUTFILE='$TEMPDIR/foo.sav'
     /VERSION=2
     .

GET /FILE='$TEMPDIR/foo.sav'.

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="check file exists"
ls -l $TEMPDIR/foo.sav > /dev/null
if [ $? -ne 0 ] ; then no_result ; fi

# Ensure that the written file has no long name table
activity="check sysfile type"
grep  'X=X' $TEMPDIR/foo.sav
if [ $? -eq 0 ] ; then fail ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: Data List
x,variable,variab_a,variab_b
1.00,1.00,1.00,2.00
1.00,1.00,2.00,30.00
1.00,2.00,1.00,8.00
1.00,2.00,2.00,20.00
2.00,1.00,1.00,2.00
2.00,1.00,2.00,22.00
2.00,2.00,1.00,1.00
2.00,2.00,2.00,3.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
