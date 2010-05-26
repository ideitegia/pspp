#! /bin/sh

TEMPDIR=/tmp/pspp-tst-$$
mkdir -p $TEMPDIR
trap 'cd /; rm -rf $TEMPDIR' 0

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT
: ${PERL:=perl}

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH

fail()
{
    echo $activity
    echo FAILED
    exit 1;
}


no_result()
{
    echo $activity
    echo NO RESULT;
    exit 2;
}

pass()
{
    exit 0;
}

cd $TEMPDIR

activity="generate pspp syntax"
$PERL $top_srcdir/tests/formats/num-out.pl > num-out.pspp
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv num-out.pspp
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="inexactify results"
$top_builddir/tests/formats/inexactify < output.txt > output.inexact
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="gunzip expected results"
gzip -cd < $top_srcdir/tests/formats/num-out.expected.cmp.gz > expected.txt.cmp
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="decompress expected results"
$PERL $top_srcdir/tests/formats/num-out-decmp.pl < expected.txt.cmp > expected.txt
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="inexactify expected results"
$top_builddir/tests/formats/inexactify < expected.txt > expected.inexact
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="compare output"
$PERL $top_srcdir/tests/formats/num-out-compare.pl \
	$PSPP_NUM_OUT_COMPARE_FLAGS expected.inexact output.inexact
if [ $? -ne 0 ] ; then fail ; fi

echo .

pass
