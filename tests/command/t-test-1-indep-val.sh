#!/bin/sh

# This program tests that the T-TEST /GROUPS command works properly 
# when a single value in the independent variable is given.

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
data list list /INDEP * DEP *.
begin data.
       1        6
       1        6
       1        7
       1        6
       1       13
       1        4
       1        7
       1        9
       1        7
       1       12
       1       11
       2       11
       2        9
       2        8
       2        4
       2       16
       2        9
       2        9
       2        5
       2        4
       2       10
       2       14
end data.
t-test /groups=indep(1.514) /var=dep.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
INDEP,F8.0
DEP,F8.0

Table: Group Statistics
,INDEP,N,Mean,Std. Deviation,S.E. Mean
DEP,>=1.514,11,9.00,3.82,1.15
,<1.514,11,8.00,2.86,.86

Table: Independent Samples Test
,,Levene's Test for Equality of Variances,,t-test for Equality of Means,,,,,,
,,,,,,,,,95% Confidence Interval of the Difference,
,,F,Sig.,t,df,Sig. (2-tailed),Mean Difference,Std. Error Difference,Lower,Upper
DEP,Equal variances assumed,.17,.68,-.69,20.00,.50,-1.00,1.44,-4.00,2.00
,Equal variances not assumed,,,-.69,18.54,.50,-1.00,1.44,-4.02,2.02
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
