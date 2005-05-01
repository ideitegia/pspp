#!/bin/sh

# This program tests for a bug which crashed pspp when doing a aggregate 
# procedure

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

activity="create program"
cat > $TESTFILE <<EOF
INPUT PROGRAM.
LOOP c=1 TO 20.
  COMPUTE x=UNIFORM(10)
  END CASE.
END LOOP.
END FILE.
END INPUT PROGRAM.

AGGREGATE /BREAK=x .
EOF
if [ $? -ne 0 ] ; then no_result ; fi


# The above input is invalid.
# So this will have a non zero error status.
# But it shouldn't crash!
activity="run_program"
$SUPERVISOR $here/../src/pspp $TESTFILE > /dev/null
if [ $? -ne 1 ] ; then fail ; fi


pass;
