#!/bin/sh

# This program tests for a bug in the VALUE LABELS command
# which caused a crash if it had a trailing /

TEMPDIR=/tmp/pspp-tst-$$

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
cat > $TEMPDIR/out.stat <<EOF
DATA LIST LIST /X * .
BEGIN DATA.
1 
2
3
4
END DATA.


VALUE LABELS X 1 'one' 2 'two' 3 'three'/


LIST VARIABLES=X.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/out.stat
if [ $? -ne 0 ] ; then fail ; fi

pass
