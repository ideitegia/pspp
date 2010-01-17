#!/bin/sh

# This program tests  the ROC command.

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
cat > $TESTFILE <<EOF
set format F10.3.
data list notable list /x * y * w * a *.
begin data.
1 1 2  1
1 2 28 0
2 3 4  1
2 4 14 0
3 5 10 1
. . 1  0
3 1 5  0
4 2 14 1
4 3 2  0
5 4 20 1
5 4 20 .
5 5 1  0
end data.

weight by w.

roc x by a (1)
	/plot = none
	/print = se coordinates
	/criteria = testpos(large) distribution(free) ci(99)
	/missing = exclude .

roc x y by a (1)
	/plot = curve(reference)
        /print = se coordinates
	/criteria = testpos(large) distribution(negexpo) ci(95)
	/missing = exclude .
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: Case Summary
,Valid N (listwise),
a,Unweighted,Weighted
Positive,5,50.000
Negative,5,50.000

Table: Area Under the Curve (x)
,,,Asymp. 99% Confidence Interval,
Area,Std. Error,Asymptotic Sig.,Lower Bound,Upper Bound
.910,.030,.000,.839,.981

Table: Coordinates of the Curve (x)
Positive if greater than or equal to,Sensitivity,1 - Specificity
.000,1.000,1.000
1.500,.960,.440
2.500,.880,.160
3.500,.680,.060
4.500,.400,.020
6.000,.000,.000

Table: Case Summary
,Valid N (listwise),
a,Unweighted,Weighted
Positive,5,50.000
Negative,5,50.000

Table: Area Under the Curve
,,,,Asymp. 95% Confidence Interval,
Variable under test,Area,Std. Error,Asymptotic Sig.,Lower Bound,Upper Bound
x,.910,.030,.000,.860,.960
y,.697,.052,.001,.611,.783

Table: Coordinates of the Curve
Test variable,Positive if greater than or equal to,Sensitivity,1 - Specificity
x,.000,1.000,1.000
,1.500,.960,.440
,2.500,.880,.160
,3.500,.680,.060
,4.500,.400,.020
,6.000,.000,.000
y,.000,1.000,1.000
,1.500,.960,.900
,2.500,.680,.340
,3.000,.600,.340
,3.500,.600,.300
,4.500,.200,.020
,6.000,.000,.000
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
