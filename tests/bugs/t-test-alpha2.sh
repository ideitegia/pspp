#!/bin/sh

# This program tests that the T-TEST fails gracefully when 
#  a single alpha variable is specified for the independent variable

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
cat > $TEMPDIR/out.stat <<EOF
data list list /id * indep (a1) dep1 * dep2 *.
begin data.
1  'a' 1 3
2  'a' 2 4
3  'a' 2 4 
4  'a' 2 4 
5  'a' 3 5
6  'b' 3 1
7  'b' 4 2
8  'b' 4 2
9  'b' 4 2
10 'b' 5 3
11 'c' 2 2
end data.


t-test /GROUPS=indep('a') /var=dep1 dep2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/out.stat > /dev/null
#invert  v
if [ $? -eq 0 ] ; then fail ; fi


pass
