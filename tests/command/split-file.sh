#!/bin/sh

# This program tests the split file command

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
cat > $TEMPDIR/split.stat <<EOF
title 'Test SPLIT FILE utility'.

data list notable /x 1 y 2.
begin data.
12
16
17
19
15
14
27
20
26
25
28
29
24
end data.
split file by x.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$here/../src/pspp -o raw-ascii $TEMPDIR/split.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
diff -B -b $TEMPDIR/pspp.list - <<EOF
Variable Value Label
X            1

X Y
- -
1 2 
1 6 
1 7 
1 9 
1 5 
1 4 

Variable Value Label
X            2

X Y
- -
2 7 
2 0 
2 6 
2 5 
2 8 
2 9 
2 4
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
