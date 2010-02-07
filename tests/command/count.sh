#!/bin/sh

# This program tests the count transformation

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

activity="Create File 1"
cat > $TESTFILE <<EOF
title 'Test COUNT transformation'.

* we're going to count the 2s 4s and 1s in the data
data list /V1 to V2 1-4(a).
begin data.
1234
321      <----
2 13     <----
4121
1104     ---- this is not '4', but '04' (v1 and v2 are string format )
03 4     <----
0193
end data.
count C=v1 to v2('2',' 4','1').
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="Run pspp 1"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results 1"
diff -c $TEMPDIR/pspp.csv - <<EOF
Title: Test COUNT transformation

Table: Reading 1 record from INLINE.
Variable,Record,Columns,Format
V1,1,1-  2,A2
V2,1,3-  4,A2

Table: Data List
V1,V2,C
12,34,.00
32,1 ,1.00
2 ,13,1.00
41,21,.00
11,04,.00
03,4,1.00
01,93,.00
EOF
if [ $? -ne 0 ] ; then no_result ; fi




activity="Create file 2"
cat > $TESTFILE <<EOF
data list list /x * y *.
begin data.
1 2
2 3
4 5
2 2
5 6
end data.

count C=x y (2).

list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="Run pspp 2"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
x,F8.0
y,F8.0

Table: Data List
x,y,C
1.00,2.00,1.00
2.00,3.00,1.00
4.00,5.00,.00
2.00,2.00,2.00
5.00,6.00,.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
