#!/bin/sh

# This program tests that the rename command works properly

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

PSPP=$top_builddir/src/ui/terminal/pspp


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
DATA LIST LIST /brakeFluid * y * .
BEGIN DATA.
1 3
2 3
3 3
4 3
END DATA.

LIST.

RENAME VARIABLES (brakeFluid=applecarts).

LIST.

SAVE /OUTFILE='$TEMPDIR/rename.sav'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="check sysfile"
grep -i Brake $TEMPDIR/rename.sav
if [ $? -eq 0 ] ; then fail ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
brakeFluid,F8.0
y,F8.0

Table: Data List
brakeFluid,y
1.00,3.00
2.00,3.00
3.00,3.00
4.00,3.00

Table: Data List
applecarts,y
1.00,3.00
2.00,3.00
3.00,3.00
4.00,3.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
