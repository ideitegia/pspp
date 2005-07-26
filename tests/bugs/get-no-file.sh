#!/bin/sh

# This program tests for a bug which caused a crash when 
# GET specified a non-existent file

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
cat > $TESTFILE <<EOF
GET /FILE='$TEMPDIR/no-file.xx'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


# The command should produce a warning.  Not an error.
# We use the stdinput here, because the bug seems to manifest itself only in 
# interactive mode.
activity="run program"
cat $TESTFILE | $SUPERVISOR $here/../src/pspp -o raw-ascii  > /dev/null
if [ $? -ne 0 ] ; then fail ; fi

pass
