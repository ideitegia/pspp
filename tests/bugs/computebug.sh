#!/bin/sh

# This program tests for a bug in the `compute' command

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
if [ $? -ne 0 ] ; then no_result ; fi

activity="create input"
cat > $TESTFILE <<EOF
DATA LIST LIST
 /A (A161)
 B (A3).

BEGIN DATA
abc   def
ghi   jkl
END DATA.

COMPUTE A=upcase(A).
EXECUTE.
LIST.
EOF

activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
A,A161
B,A3

Table: Data List
A,B
ABC                                                                                                                                                              ,def
GHI                                                                                                                                                              ,jkl
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
