#!/bin/sh

# This program tests that the T-TEST /GROUPS command works

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
data list list /ID * INDEP * DEP1 * DEP2 *.
begin data.
1  1.1 1 3
2  1.1 2 4
3  1.1 2 4 
4  1.1 2 4 
5  1.1 3 5
6  2.1 3 1
7  2.1 4 2
8  2.1 4 2
9  2.1 4 2
10 2.1 5 3
11 3.1 2 2
end data.

* Note that this last case should be IGNORED since it doesn't have a dependent variable of either 1 or 2

t-test /GROUPS=indep(1.1,2.1) /var=dep1 dep2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Reading free-form data from INLINE.
Variable,Format
ID,F8.0
INDEP,F8.0
DEP1,F8.0
DEP2,F8.0

Table: Group Statistics
,INDEP,N,Mean,Std. Deviation,S.E. Mean
DEP1,1.1,5,2.00,.71,.32
,2.1,5,4.00,.71,.32
DEP2,1.1,5,4.00,.71,.32
,2.1,5,2.00,.71,.32

Table: Independent Samples Test
,,Levene's Test for Equality of Variances,,t-test for Equality of Means,,,,,,
,,,,,,,,,95% Confidence Interval of the Difference,
,,F,Sig.,t,df,Sig. (2-tailed),Mean Difference,Std. Error Difference,Lower,Upper
DEP1,Equal variances assumed,.00,1.00,-4.47,8.00,.00,-2.00,.45,-3.03,-.97
,Equal variances not assumed,,,-4.47,8.00,.00,-2.00,.45,-3.03,-.97
DEP2,Equal variances assumed,.00,1.00,4.47,8.00,.00,2.00,.45,.97,3.03
,Equal variances not assumed,,,4.47,8.00,.00,2.00,.45,.97,3.03
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
