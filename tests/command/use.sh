#!/bin/sh

# This program tests USE, just to make sure that USE ALL is accepted silently.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
     cd /
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
cat > $TEMPDIR/filter.stat << EOF
data list notable /X 1-2.
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
use all.
list.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TEMPDIR/filter.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="check results"
perl -pi -e s/^\s*\$//g $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - << EOF | perl -e 's/^\s*$//g'
 X
--
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
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
