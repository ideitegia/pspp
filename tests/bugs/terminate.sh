#!/bin/sh

# This  tests checks that when a fatal error occurs,
# and appropriate notice is printed and the program exits with a 
# non zero status


TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
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


activity="delete file"
rm -f $TEMPDIR/bar.dat
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program"
cat > $TEMPDIR/foo.sps <<EOF
DATA LIST FILE='$TEMPDIR/bar.dat' /S 1-2 (A) X 3 .

EXECUTE.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
# This must exit with non zero status
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/foo.sps > /dev/null 2> $TEMPDIR/stderr
if [ $? -eq 0 ] ; then fail ; fi

activity="compare stderr"
diff $TEMPDIR/stderr - << EOF
pspp: Terminating NOW due to a fatal error!
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
