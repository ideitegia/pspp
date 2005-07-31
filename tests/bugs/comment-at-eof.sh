#!/bin/sh

# Tests for a bug wherein a comment just before end-of-file caused an
# infinite loop.  Thus, this test passes as long as it completes.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

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

activity="create program"
cat > $TEMPDIR/foo.sps <<EOF
COMMENT this is a comment at end of file.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/foo.sps
if [ $? -ne 0 ] ; then fail; fi

pass;
