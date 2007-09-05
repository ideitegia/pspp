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
$PERL -e 'print pack "n", $_ foreach 0...65535' > legacy-in.data

activity="write pspp syntax"
cat > legacy-in.pspp <<'EOF'
SET ERRORS=NONE.
SET MXWARNS=10000000.
SET MXERRS=10000000.
FILE HANDLE data/NAME='legacy-in.data'/MODE=IMAGE/LRECL=2.
DATA LIST FILE=data/n 1-2 (N) z 1-2 (z).
COMPUTE x=$CASENUM - 1.
PRINT OUTFILE='legacy-in.out'/x (PIBHEX4) ' ' N Z.
EXECUTE.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode legacy-in.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="gunzip expected results"
gzip -cd < $top_srcdir/tests/formats/legacy-in.expected.cmp.gz > legacy-in.expected.cmp
if [ $? -ne 0 ] ; then no_result ; fi

activity="decompress expected results"
$PERL -pe "printf ' %04X ', $.-1" < legacy-in.expected.cmp > legacy-in.expected
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -u legacy-in.expected legacy-in.out
if [ $? -ne 0 ] ; then fail ; fi

pass
