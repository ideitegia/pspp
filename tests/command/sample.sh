#!/bin/sh

# This program tests the SAMPLE function

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
cat > $TEMPDIR/sample.stat <<EOF
set seed=2

data list /a 1-2.
begin data.
1
2
3
4
5
6
7
8
9
10
end data.
sample .5.
n 5.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


$here/../src/pspp -o raw-ascii $TEMPDIR/sample.stat
if [ $? -ne 0 ] ; then fail ; fi

fail


pass;
