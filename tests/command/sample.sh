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
set seed=3

data list notable /a 1-2.
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
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii --testing-mode $TEMPDIR/sample.stat 
if [ $? -ne 0 ] ; then no_result ; fi

activity="create head"
grep -v '^\ *$' $TEMPDIR/pspp.list | head -2 > $TEMPDIR/head
if [ $? -ne 0 ] ; then no_result ; fi

activity="extract data"
grep  '[0-9][0-9]*' $TEMPDIR/pspp.list > $TEMPDIR/data
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare head"
diff -B -b $TEMPDIR/head - << EOF
 A
--
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="compare data"
diff -w $TEMPDIR/data - << EOF > $TEMPDIR/diffs
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
# note   vv 
if [ $? -eq 0 ] ; then fail ; fi

# Check that there was nothing added
grep '^<' $TEMPDIR/diffs
# note   vv
if [ $? -eq 0 ] ; then fail ; fi


pass;
