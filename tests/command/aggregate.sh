#!/bin/sh

# This program tests the aggregate procedure

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

activity="program create"
cat > $TESTFILE << EOF

data list notable /x y 1-2.
begin data.
13
27
30
12
26
11
10
28
29
14
15
end data.
sort cases by x.
aggregate /missing=columnwise /document /presorted/break=x(a) /z'label for z'=sum(y)/foo=nu.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp    -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="test result"
diff  -b -w -B $TEMPDIR/pspp.list - << EOF
X        Z      FOO
- -------- --------
1    15.00     6.00 
2    30.00     4.00 
3      .00     1.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
