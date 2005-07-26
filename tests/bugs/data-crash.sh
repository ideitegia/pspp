#!/bin/sh

# This program tests for a bug which crashed pspp when given certain
# invalid input

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


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

activity="create program"
cat > $TEMPDIR/ct.stat <<EOF
DATA rubbish.
EXECUTE.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

#This must fail
activity="run program"
$SUPERVISOR $here/../src/pspp $TEMPDIR/ct.stat > /dev/null
if [ $? -ne 1 ] ; then fail ; fi


pass;
