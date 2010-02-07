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
$PERL -e 'print pack "n", $_ foreach 0...65535' > bcd-in.data

activity="write pspp syntax"
cat > bcd-in.pspp <<'EOF'
SET ERRORS=NONE.
SET MXWARNS=10000000.
SET MXERRS=10000000.
FILE HANDLE data/NAME='bcd-in.data'/MODE=IMAGE/LRECL=2.
DATA LIST FILE=data/p 1-2 (P) pk 1-2 (PK).
COMPUTE x=$CASENUM - 1.
PRINT OUTFILE='bcd-in.out'/x (PIBHEX4) ' ' P PK.
EXECUTE.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv bcd-in.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="gunzip expected results"
gzip -cd < $top_srcdir/tests/formats/bcd-in.expected.cmp.gz > bcd-in.expected.cmp
if [ $? -ne 0 ] ; then no_result ; fi

activity="decompress expected results"
$PERL -pe "printf ' %04X ', $.-1" < bcd-in.expected.cmp > bcd-in.expected
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -u bcd-in.expected bcd-in.out
if [ $? -ne 0 ] ; then fail ; fi

pass
