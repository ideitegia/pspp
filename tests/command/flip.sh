#!/bin/sh

# This program tests the flip command

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

activity="create flip.stat"
cat > $TEMPDIR/flip.stat <<EOF
* Test FLIP with NEWNAME or, equivalently, with a variable named CASE_LBL.
data list notable /N 1 (a) A B C D 2-9.
list.
begin data.
v 1 2 3 4 5
w 6 7 8 910
x1112131415
y1617181920
z2122232425
end data.
flip newnames=n.
list.
flip.
list.

* Test FLIP without NEWNAME.
data list list notable /v1 to v10.
format all(f2).
begin data.
1 2 3 4 5 6 7 8 9 10
4 5 6 7 8 9 10 11 12 13
end data.

list.

flip.
list. 
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/flip.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: Data List
N,A,B,C,D
v,1,2,3,4
w,6,7,8,9
x,11,12,13,14
y,16,17,18,19
z,21,22,23,24

Table: Data List
CASE_LBL,V,W,X,Y,Z
A       ,1.00,6.00,11.00,16.00,21.00
B       ,2.00,7.00,12.00,17.00,22.00
C       ,3.00,8.00,13.00,18.00,23.00
D       ,4.00,9.00,14.00,19.00,24.00

Table: Data List
CASE_LBL,A,B,C,D
V       ,1.00,2.00,3.00,4.00
W       ,6.00,7.00,8.00,9.00
X       ,11.00,12.00,13.00,14.00
Y       ,16.00,17.00,18.00,19.00
Z       ,21.00,22.00,23.00,24.00

Table: Data List
v1,v2,v3,v4,v5,v6,v7,v8,v9,v10
1,2,3,4,5,6,7,8,9,10
4,5,6,7,8,9,10,11,12,13

Table: Data List
CASE_LBL,VAR000,VAR001
v1      ,1.00,4.00
v2      ,2.00,5.00
v3      ,3.00,6.00
v4      ,4.00,7.00
v5      ,5.00,8.00
v6      ,6.00,9.00
v7      ,7.00,10.00
v8      ,8.00,11.00
v9      ,9.00,12.00
v10     ,10.00,13.00
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
