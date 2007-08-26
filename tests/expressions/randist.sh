#! /bin/sh

# Tests random distribution functions.

TEMPDIR=/tmp/pspp-tst-$$

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp


STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
        echo NOT removing directory $TEMPDIR
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

activity="run script to generate random distribution test command file"
perl $top_srcdir/tests/expressions/randist/randist.pl \
    < $top_srcdir/tests/expressions/randist/randist.txt \
    > randist.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="run command file"
$SUPERVISOR $PSPP --testing-mode \
    $TEMPDIR/randist.pspp >$TEMPDIR/randist.err 2> $TEMPDIR/randist.out
if [ $? -ne 0 ] ; then fail ; fi

for d in beta cauchy chisq exp f gamma laplace logistic lnormal \
	 normal pareto t uniform weibull; do
    activity="compare output for $d distribution"
    perl $top_srcdir/tests/expressions/randist/compare.pl \
	$top_srcdir/tests/expressions/randist/$d.out $TEMPDIR/$d.out
    if [ $? -ne 0 ] ; then fail ; fi
done

pass
