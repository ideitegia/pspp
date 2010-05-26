#!/bin/sh

# This program tests for a bug in which examine didn't
# count missing values.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`

PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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

activity="create program 1"
cat > $TESTFILE << EOF
DATA LIST LIST /x * y *.
BEGIN DATA.
1   1 
2   1
3   1
4   1
5   2
6   2
.   2
END DATA

EXAMINE /x by y.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
x,F8.0
y,F8.0

Table: Case Processing Summary
,Cases,,,,,
,Valid,,Missing,,Total,
,N,Percent,N,Percent,N,Percent
x,6,85.7143%,1,14.2857%,7,100%

Table: Case Processing Summary
,,Cases,,,,,
,,Valid,,Missing,,Total,
,y,N,Percent,N,Percent,N,Percent
x,1.00,4,100%,0,0%,4,100%
,2.00,2,66.6667%,1,33.3333%,3,100%
EOF
if [ $? -ne 0 ] ; then fail ; fi


#Make sure this doesn't interfere with percentiles operation.

activity="create program 2"
cat > $TESTFILE << EOF
DATA LIST LIST /X *.
BEGIN DATA.
99
99
5.00
END DATA.

MISSING VALUE X (99).

EXAMINE /x
        /PERCENTILES=HAVERAGE.


EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 2"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi


pass;
