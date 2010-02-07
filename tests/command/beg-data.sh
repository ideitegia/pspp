#!/bin/sh

# This program tests the BEGIN DATA / END DATA commands

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
cat > $TESTFILE << EOF_foobar
title 'Test BEGIN DATA ... END DATA'.

data list /A B 1-2.
list.
begin data.
12
34
56
78
90
end data.

data list /A B 1-2.
begin data.
09
87
65
43
21
end data.
list.
EOF_foobar
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare data"
diff -c $TEMPDIR/pspp.csv - << foobar
Title: Test BEGIN DATA ... END DATA

Table: Reading 1 record from INLINE.
Variable,Record,Columns,Format
A,1,1-  1,F1.0
B,1,2-  2,F1.0

Table: Data List
A,B
1,2
3,4
5,6
7,8
9,0

Table: Reading 1 record from INLINE.
Variable,Record,Columns,Format
A,1,1-  1,F1.0
B,1,2-  2,F1.0

Table: Data List
A,B
0,9
8,7
6,5
4,3
2,1
foobar
if [ $? -ne 0 ] ; then fail ; fi


pass;
