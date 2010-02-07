#!/bin/sh

# This program tests ....

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

activity="create syntax 1"
cat > recode-copy-bug-1.stat <<EOF
TITLE 'Test for regression of recode COPY bug'

DATA LIST LIST
 /A (A1)
 B (A1).

BEGIN DATA
1     2
2     3
3     4
END DATA.

** Clearly, the else=copy is superfluous here
RECODE A ("1"="3") ("3"="1") (ELSE=COPY).
EXECUTE.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create syntax 2" 
cat > recode-copy-bug-2.stat <<EOF
DATA LIST LIST
 /A (A1)
 B (A1).

BEGIN DATA
1     2
2     3
3     4
END DATA.

STRING A1 (A1).
RECODE A ("1"="3") ("3"="1") (ELSE=COPY) INTO a1.
EXECUTE.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 1"
$SUPERVISOR $PSPP -o pspp.csv recode-copy-bug-1.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output 1"
diff -c $TEMPDIR/pspp.csv - <<EOF
Title: Test for regression of recode COPY bug

Table: Reading free-form data from INLINE.
Variable,Format
A,A1
B,A1

Table: Data List
A,B
3,2
2,3
1,4
EOF
if [ $? -ne 0 ] ; then fail ; fi


activity="run program 2"
$SUPERVISOR $PSPP -o pspp.csv recode-copy-bug-2.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output 2"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
A,A1
B,A1

Table: Data List
A,B,A1
1,2,3
2,3,2
3,4,1
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
