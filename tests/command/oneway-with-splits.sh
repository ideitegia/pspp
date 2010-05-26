#!/bin/sh

# This program tests that the ONEWAY anova command works OK
# when SPLIT FILE is active

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
DATA LIST LIST /QUALITY * BRAND * S *.
BEGIN DATA
3 1 1
2 1 1
1 1 1
1 1 1
4 1 1
5 2 1
2 2 1
4 2 2
2 2 2
3 2 2
7  3 2
4  3 2
5  3 2
3  3 2
6  3 2
END DATA

VARIABLE LABELS brand 'Manufacturer'.
VARIABLE LABELS quality 'Breaking Strain'.

VALUE LABELS /brand 1 'Aspeger' 2 'Bloggs' 3 'Charlies'.

SPLIT FILE by s.

ONEWAY
	quality BY brand
	/STATISTICS descriptives homogeneity
	/CONTRAST =  -2 2
	/CONTRAST = -1 1
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
S,F8.0

Variable,Value,Label
S,1.00,

Table: Descriptives
,,,,,,95% Confidence Interval for Mean,,,
,,N,Mean,Std. Deviation,Std. Error,Lower Bound,Upper Bound,Minimum,Maximum
Breaking Strain,Aspeger,5,2.20,1.30,.58,.58,3.82,1.00,4.00
,Bloggs,2,3.50,2.12,1.50,-15.56,22.56,2.00,5.00
,Total,7,2.57,1.51,.57,1.17,3.97,1.00,5.00

Table: Test of Homogeneity of Variances
,Levene Statistic,df1,df2,Significance
Breaking Strain,1.09,1,5,.35

Table: ANOVA
,,Sum of Squares,df,Mean Square,F,Significance
Breaking Strain,Between Groups,2.41,1,2.41,1.07,.35
,Within Groups,11.30,5,2.26,,
,Total,13.71,6,,,

Table: Contrast Coefficients
,,Manufacturer,
,,Aspeger,Bloggs
Contrast,1,-2,2
,2,-1,1

Table: Contrast Tests
,,Contrast,Value of Contrast,Std. Error,t,df,Sig. (2-tailed)
Breaking Strain,Assume equal variances,1,2.60,2.52,1.03,5,.35
,,2,1.30,1.26,1.03,5,.35
,Does not assume equal,1,2.60,3.22,.81,1.32,.54
,,2,1.30,1.61,.81,1.32,.54

Variable,Value,Label
S,2.00,

Table: Descriptives
,,,,,,95% Confidence Interval for Mean,,,
,,N,Mean,Std. Deviation,Std. Error,Lower Bound,Upper Bound,Minimum,Maximum
Breaking Strain,Bloggs,3,3.00,1.00,.58,.52,5.48,2.00,4.00
,Charlies,5,5.00,1.58,.71,3.04,6.96,3.00,7.00
,Total,8,4.25,1.67,.59,2.85,5.65,2.00,7.00

Table: Test of Homogeneity of Variances
,Levene Statistic,df1,df2,Significance
Breaking Strain,.92,1,6,.37

Table: ANOVA
,,Sum of Squares,df,Mean Square,F,Significance
Breaking Strain,Between Groups,7.50,1,7.50,3.75,.10
,Within Groups,12.00,6,2.00,,
,Total,19.50,7,,,

Table: Contrast Coefficients
,,Manufacturer,
,,Bloggs,Charlies
Contrast,1,-2,2
,2,-1,1

Table: Contrast Tests
,,Contrast,Value of Contrast,Std. Error,t,df,Sig. (2-tailed)
Breaking Strain,Assume equal variances,1,4.00,2.07,1.94,6,.10
,,2,2.00,1.03,1.94,6,.10
,Does not assume equal,1,4.00,1.83,2.19,5.88,.07
,,2,2.00,.91,2.19,5.88,.07
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
