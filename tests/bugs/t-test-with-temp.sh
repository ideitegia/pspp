#!/bin/sh

# This program tests that the T-TEST 
# works ok with a TEMPORARY transformation

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
data list list /ind * x * .
begin data.
1 3.5
1 2.0
1 2.0
2 3.5
2 3.0
2 4.0
end data.

t-test /groups=ind(1,2) /var x.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="copy output"
cp $TEMPDIR/pspp.list $TEMPDIR/first.list
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 2"
cat > $TESTFILE <<EOF
data list list /ind * x * .
begin data.
1 3.5
1 2.0
1 2.0
2 3.5
2 3.0
2 4.0
2 9.0
end data.

TEMPORARY.
SELECT IF ind < 7.

t-test /groups=ind(1,2) /var x.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -B -b $TEMPDIR/pspp.list $TEMPDIR/first.list
if [ $? -ne 0 ] ; then fail ; fi


pass
