#!/bin/sh

# This program tests for a bug which caused a crash after SAVE FILE
# was called on a COMPUTEd variable


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
cat > $TEMPDIR/foo.sps <<EOF
INPUT PROGRAM.
	COMPUTE num = 3.
END FILE.
END INPUT PROGRAM.
EXECUTE.

SAVE outfile='$TEMPDIR/temp.sav'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/foo.sps
if [ $? -ne 0 ] ; then fail; fi



pass;
