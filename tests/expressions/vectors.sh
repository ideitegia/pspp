#!/bin/sh

# This program tests use of vectors in expressions.

TEMPDIR=/tmp/pspp-tst-$$

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

activity="create program"
cat > $TEMPDIR/vectors.stat <<EOF
DATA LIST /N1 TO N5 1-5.
MISSING VALUES N1 TO N5 (3 THRU 5, 1).
BEGIN DATA.
12345
6789 
END DATA.

VECTOR N=N1 TO N5.
VECTOR X(5).
LOOP I=1 TO 5.
COMPUTE X(I)=N(I) + 1.
END LOOP.

FORMATS ALL (F2).

LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/vectors.stat > $TEMPDIR/vectors.err 2> $TEMPDIR/vectors.out
if [ $? -ne 0 ] ; then fail ; fi

activity="compare results"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading 1 record from INLINE.
Variable,Record,Columns,Format
N1,1,1-  1,F1.0
N2,1,2-  2,F1.0
N3,1,3-  3,F1.0
N4,1,4-  4,F1.0
N5,1,5-  5,F1.0

Table: Data List
N1,N2,N3,N4,N5,X1,X2,X3,X4,X5,I
1,2,3,4,5,.,3,.,.,.,5
6,7,8,9,.,7,8,9,10,.,5
EOF

if [ $? -ne 0 ] ; then no_result ; fi


if [ $? -ne 0 ] ; then fail ; fi



pass;
