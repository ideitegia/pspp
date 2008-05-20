#! /bin/sh

TEMPDIR=/tmp/pspp-tst-$$

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp
: ${PERL:=perl}

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

activity="generate data input file"
$PERL -e 'print pack "n", $_ foreach 0...65535' > ib-in.data

activity="write pspp syntax"
cat > ib-in.pspp <<'EOF'
SET RIB=MSBFIRST.
SET ERRORS=NONE.
SET MXWARNS=10000000.
SET MXERRS=10000000.
FILE HANDLE data/NAME='ib-in.data'/MODE=IMAGE/LRECL=2.
DATA LIST FILE=data/ib 1-2 (IB) pib 1-2 (PIB) pibhex 1-2 (PIBHEX).
COMPUTE x=$CASENUM - 1.
PRINT OUTFILE='ib-in.out'/x (PIBHEX4) ' ' ib pib pibhex.
EXECUTE.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode ib-in.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="gunzip expected results"
gzip -cd < $top_srcdir/tests/formats/ib-in.expected.cmp.gz > ib-in.expected.cmp
if [ $? -ne 0 ] ; then no_result ; fi

activity="decompress expected results"
$PERL -pe "printf ' %04X ', $.-1" < ib-in.expected.cmp > ib-in.expected
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -u ib-in.expected ib-in.out
if [ $? -ne 0 ] ; then fail ; fi

pass
