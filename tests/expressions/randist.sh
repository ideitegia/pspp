#! /bin/sh

# Tests random distribution functions.

TEMPDIR=/tmp/pspp-tst-$$

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
     rm -rf $TEMPDIR
     :
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
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii \
    $TEMPDIR/randist.pspp >$TEMPDIR/randist.err 2> $TEMPDIR/randist.out
if [ $? -ne 0 ] ; then fail ; fi

for d in beta cauchy chisq exp f gamma laplace logistic lnormal \
	 normal pareto t uniform weibull; do
    activity="compare output for $d distribution"
    diff -B -b $top_srcdir/tests/expressions/randist/$d.out $TEMPDIR/$d.out
    if [ $? -ne 0 ] ; then fail ; fi
done

pass
