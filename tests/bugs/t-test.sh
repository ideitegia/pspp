#!/bin/sh

# This program tests for a bug which caused T-TEST to 
# crash when given invalid syntax


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
cat > $TEMPDIR/rnd.sps <<EOF
DATA LIST LIST /id * a * .
BEGIN DATA.
1 3.5
2 2.0
3 2.0
4 3.5
5 3.0
6 4.0
END DATA.

T-TEST /testval=2.0 .
T-TEST /groups=id(3) .
EOF
if [ $? -ne 0 ] ; then no_result ; fi

#The syntax was invalid.  Therefore pspp must return non zero.
activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/rnd.sps > /dev/null
if [ $? -ne 1 ] ; then fail ; fi

pass;
