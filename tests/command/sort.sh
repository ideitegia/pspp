#!/bin/sh

# This program tests the sort command

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


activity="generate stat program"
cat > $TEMPDIR/sort.stat <<EOF
title 'Test SORT procedure'.

data list file='$here/sort.data' notable /X000 to X126 1-127.
sort by X000 to x005.
print /X000 to X005.
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/sort.stat
if [ $? -ne 0 ] ; then no_result ; fi

# Now there should be some sorted data in $TEMPDIR/pspp.list
# We have to do some checks on it.

# 1. Is it sorted ?

activity="check sorted"
sort $TEMPDIR/pspp.list  > $TEMPDIR/sortsort
if [ $? -ne 0 ] ; then no_result ; fi

diff -B -b $TEMPDIR/sortsort $TEMPDIR/pspp.list
if [ $? -ne 0 ] ; then fail ; fi

# 2. It should be six elements wide
activity="check data width"
awk '!/^\ *$/{if (NF!=6) exit 1}' $TEMPDIR/pspp.list 
if [ $? -ne 0 ] ; then fail ; fi


pass;
