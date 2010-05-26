#!/bin/sh

# This program tests that the ONEWAY anova command works OK

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


cleanup()
{
    if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo Not cleaning $TEMPDIR;
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
cat > $TESTFILE <<EOF
DATA LIST LIST /QUALITY * BRAND * .
BEGIN DATA
3 1
2 1
1 1
1 1
4 1
5 2
2 2
4 2
2 2
3 2
7  3
4  3
5  3
3  3
6  3
END DATA

VARIABLE LABELS brand 'Manufacturer'.
VARIABLE LABELS quality 'Breaking Strain'.

VALUE LABELS /brand 1 'Aspeger' 2 'Bloggs' 3 'Charlies'.

ONEWAY
	quality BY brand
	/STATISTICS descriptives homogeneity
	/CONTRAST =  -2 1 1 
	/CONTRAST = 0 -1 1
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

diff -c $TEMPDIR/pspp.csv - << EOF
Table: Reading free-form data from INLINE.
Variable,Format
QUALITY,F8.0
BRAND,F8.0

Table: Descriptives
,,,,,,95% Confidence Interval for Mean,,,
,,N,Mean,Std. Deviation,Std. Error,Lower Bound,Upper Bound,Minimum,Maximum
Breaking Strain,Aspeger,5,2.20,1.30,.58,.58,3.82,1.00,4.00
,Bloggs,5,3.20,1.30,.58,1.58,4.82,2.00,5.00
,Charlies,5,5.00,1.58,.71,3.04,6.96,3.00,7.00
,Total,15,3.47,1.77,.46,2.49,4.45,1.00,7.00

Table: Test of Homogeneity of Variances
,Levene Statistic,df1,df2,Significance
Breaking Strain,.09,2,12,.91

Table: ANOVA
,,Sum of Squares,df,Mean Square,F,Significance
Breaking Strain,Between Groups,20.13,2,10.07,5.12,.02
,Within Groups,23.60,12,1.97,,
,Total,43.73,14,,,

Table: Contrast Coefficients
,,Manufacturer,,
,,Aspeger,Bloggs,Charlies
Contrast,1,-2,1,1
,2,0,-1,1

Table: Contrast Tests
,,Contrast,Value of Contrast,Std. Error,t,df,Sig. (2-tailed)
Breaking Strain,Assume equal variances,1,3.80,1.54,2.47,12,.03
,,2,1.80,.89,2.03,12,.07
,Does not assume equal,1,3.80,1.48,2.56,8.74,.03
,,2,1.80,.92,1.96,7.72,.09
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
