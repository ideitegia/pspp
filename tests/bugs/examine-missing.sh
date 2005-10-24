#!/bin/sh

# This program tests for a bug which caused EXAMINE to 
# crash if all its values were SYSMIS


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
cat > $TESTFILE <<EOF
DATA LIST LIST /x *.
BEGIN DATA.
.
.
.
.
END DATA.

EXAMINE /x PLOT=HISTOGRAM.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 0 ] ; then fail ; fi

pass;
