#!/bin/sh

# This program tests that both long and short variable names are parsed OK.

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

# Use crosstabs, since its TABLES subcommand exercises the array var set 
# feature.
activity="create program"
cat > $TESTFILE <<EOF
DATA LIST LIST /AlphaBetaGamma * B * X * Yabbadabbadoo * .
BEGIN DATA.
2 3 4 5
END DATA.

LIST.

CROSSTABS 
	VARIABLES X (1,7) Yabbadabbadoo (1,7) 
	/TABLES X BY Yabbadabbadoo.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
AlphaBetaGamma,F8.0
B,F8.0
X,F8.0
Yabbadabbadoo,F8.0

Table: Data List
AlphaBetaGamma,B,X,Yabbadabbadoo
2.00,3.00,4.00,5.00

Table: Summary.
,Cases,,,,,
,Valid,,Missing,,Total,
,N,Percent,N,Percent,N,Percent
X * Yabbadabbadoo,1,100.0%,0,0.0%,1,100.0%

Table: X * Yabbadabbadoo [count].
,Yabbadabbadoo,,,,,,,
X,1.00,2.00,3.00,4.00,5.00,6.00,7.00,Total
1.00,.0,.0,.0,.0,.0,.0,.0,.0
2.00,.0,.0,.0,.0,.0,.0,.0,.0
3.00,.0,.0,.0,.0,.0,.0,.0,.0
4.00,.0,.0,.0,.0,1.0,.0,.0,1.0
5.00,.0,.0,.0,.0,.0,.0,.0,.0
6.00,.0,.0,.0,.0,.0,.0,.0,.0
7.00,.0,.0,.0,.0,.0,.0,.0,.0
Total,.0,.0,.0,.0,1.0,.0,.0,1.0
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
