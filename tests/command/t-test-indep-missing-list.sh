#!/bin/sh

# This program tests that the T-TEST /GROUPS command works OK
# when LISTWISE missing values are involved

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

activity="create program 1"
cat > $TESTFILE <<EOF
data list list /id * indep * dep1 * dep2.
begin data.
1 1.0 3.5 6
2 1.0 2.0 5
3 1.0 2.0 4
4 2.0 3.5 3
5 2.0 3.0 2
6 2.0 4.0 1
end data.

t-test /group=indep /var=dep1 dep2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="copy output"
cp $TEMPDIR/pspp.list $TEMPDIR/ref.list
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 2"
cat > $TESTFILE <<EOF
data list list /id * indep * dep1 * dep2 *.
begin data.
1 1.0 3.5 6
2 1.0 2.0 5
3 1.0 2.0 4
4 2.0 3.5 3
5 2.0 3.0 2
6 2.0 4.0 1
7 2.0 .   0
end data.

t-test /missing=listwise,exclude /group=indep /var=dep1, dep2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 2"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare outputs"
diff $TEMPDIR/ref.list $TEMPDIR/pspp.list 
if [ $? -ne 0 ] ; then fail ; fi


pass
