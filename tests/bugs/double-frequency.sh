#!/bin/sh

# This program tests for a bug where pspp would crash if two frequencies
# Commands existed in a input file

TEMPDIR=/tmp/pspp-tst-$$

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

here=`pwd`;

activity="create data"
cat << EOF > $TEMPDIR/ff.stat 

data list free /v1 v2.
begin data.
0 1
2 3 
4 5
3 4
end data.

frequencies v1 v2.
frequencies v1 v2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

cd $TEMPDIR

activity="run data"
$here/../src/pspp $TEMPDIR/ff.stat
if [ $? -ne 0 ] ; then fail ; fi


pass;
