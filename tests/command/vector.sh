#!/bin/sh

# This program tests the VECTOR command

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

activity="create prog"
cat > $TEMPDIR/vector.stat <<EOF
data list notable/x 1.
vector v(4).
display vector.

data list notable/x 1.
vector #vec(4, comma10.2).
display vector.

input program.
vector x(5).
data list/x5 x2 x3 x1 x4 1-5.
end input program.
display vector.

data list notable/u w x y z 1-5.
vector a=u to y.
vector b=x to z.
vector c=all.
display vector.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv -e $TEMPDIR/stdout $TEMPDIR/vector.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare stdout"
perl -pi -e 's/^\s*$//g' $TEMPDIR/stdout
diff -b $TEMPDIR/stdout  - <<EOF
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="compare results"
diff -c $TEMPDIR/pspp.csv  - <<EOF
Vector,Position,Variable,Print Format
v,1,v1,F8.2
,2,v2,F8.2
,3,v3,F8.2
,4,v4,F8.2

Vector,Position,Variable,Print Format
#vec,1,#vec1,COMMA10.2
,2,#vec2,COMMA10.2
,3,#vec3,COMMA10.2
,4,#vec4,COMMA10.2

Table: Reading 1 record from INLINE.
Variable,Record,Columns,Format
x5,1,1-  1,F1.0
x2,1,2-  2,F1.0
x3,1,3-  3,F1.0
x1,1,4-  4,F1.0
x4,1,5-  5,F1.0

Vector,Position,Variable,Print Format
x,1,x1,F8.2
,2,x2,F8.2
,3,x3,F8.2
,4,x4,F8.2
,5,x5,F8.2

Vector,Position,Variable,Print Format
a,1,u,F1.0
,2,w,F1.0
,3,x,F1.0
,4,y,F1.0
b,1,x,F1.0
,2,y,F1.0
,3,z,F1.0
c,1,u,F1.0
,2,w,F1.0
,3,x,F1.0
,4,y,F1.0
,5,z,F1.0
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
