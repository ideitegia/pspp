#!/bin/sh

# This program tests for bug #15766 (/KEEP subcommand on SAVE doesn't
# fully support ALL) and underlying problems.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.pspp

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

for mode in COMPRESSED UNCOMPRESSED; do
    activity="create program ($mode)"
    cat > $TESTFILE <<EOF
DATA LIST LIST NOTABLE 
	/a b c d e f g h i j k l m n o p q r s t u v w x y z (F2.0).
BEGIN DATA.
1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26
END DATA.
LIST.
SAVE OUTFILE='test.sav'/$mode.
GET FILE='test.sav'/KEEP=x y z all.
LIST.
EOF
    if [ $? -ne 0 ] ; then no_result ; fi

    activity="run PSPP ($mode)"
    $SUPERVISOR $PSPP --testing-mode $TESTFILE
    if [ $? -ne 0 ] ; then no_result ; fi


    activity="compare output ($mode)"
    diff -c $TEMPDIR/pspp.csv - << EOF
Table: Data List
a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z
1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26

Table: Data List
x,y,z,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w
24,25,26,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23
EOF
    if [ $? -ne 0 ] ; then fail ; fi
done

pass;
