#!/bin/sh

# This program tests for a bug which caused AGGREGATE to crash when
# the MAX function was used.

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

activity="create program"
cat > $TESTFILE <<EOF
DATA LIST LIST /X (F8.2) Y (a25).

BEGIN DATA.
87.50 foo
87.34 bar
1 bar
END DATA.



AGGREGATE OUTFILE=* /BREAK=y /X=MAX(x).
LIST /x y.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff $TEMPDIR/pspp.csv - << EOF | cat -E
Table: Reading free-form data from INLINE.
Variable,Format
X,F8.2
Y,A25

Table: Data List
X,Y
87.34,bar                      
87.50,foo                      
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
