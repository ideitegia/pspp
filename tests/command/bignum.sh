#!/bin/sh

# This program tests the use of big numbers

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

activity="create data file"
cat > $TEMPDIR/bignum.data << wizzah
0
0.1
0.5
0.8
0.9
0.999
1
2
3
4
5
12
123
1234
12345
123456
1234567
12345678
123456789
1234567890
19999999999
199999999999
1234567890123
19999999999999
199999999999999
1234567890123456
19999999999999999
123456789012345678
1999999999999999999
12345678901234567890
199999999999999999999
1234567890123456789012
19999999999999999999999
123456789012345678901234
1999999999999999999999999
12345678901234567890123456
199999999999999999999999999
1234567890123456789012345678
19999999999999999999999999999
123456789012345678901234567890
1999999999999999999999999999999
12345678901234567890123456789012
199999999999999999999999999999999
1234567890123456789012345678901234
19999999999999999999999999999999999
123456789012345678901234567890123456
1999999999999999999999999999999999999
12345678901234567890123456789012345678
199999999999999999999999999999999999999
1234567890123456789012345678901234567890
1999999999999999999999999999999999999999
1e40
1.1e40
1.5e40
1e41
1e50
1e100
1e150
1e200
1e250
1e300
1.79641e308
wizzah
if [ $? -ne 0 ] ; then no_result ; fi


activity="create program"
cat > $TEMPDIR/prog.stat <<foobar
title 'Test use of big numbers'.

*** Do the portable output.
data list file='$TEMPDIR/bignum.data'/BIGNUM 1-40.
list.

*** Do the nonportable output for fun. 
descriptives BIGNUM.
foobar
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii prog.stat
if [ $? -ne 0 ] ; then no_result ; fi

# Like the above comments say ...
# ... if we get here without crashing, then the test has passed.

pass;
