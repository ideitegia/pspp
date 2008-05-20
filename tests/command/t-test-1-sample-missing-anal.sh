#!/bin/sh

# This program tests that the T-TEST /TESTVAL command works OK
# when there are per analysis missing values involved.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR" 
	return ; 
     fi
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
data list list /id * x1 * x2.
begin data.
1 3.5 34
2 2.0 10
3 2.0 23
4 3.5 98
5 3.0 23
67 4.0 8
end data.

t-test /testval=3.0 /var=x1 x2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="copy output"
cp $TEMPDIR/pspp.list $TEMPDIR/ref.list
if [ $? -ne 0 ] ; then no_result ; fi


activity="create program 2"
cat > $TESTFILE <<EOF
data list list /id * x1 * x2.
begin data.
1 3.5 34
2 2.0 10
3 2.0 23
4 3.5 98
5 3.0 23
6 4.0 .
7  .  8
end data.

t-test /missing=analysis /testval=3.0 /var=x1 x2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 2"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare outputs"
diff $TEMPDIR/ref.list $TEMPDIR/pspp.list
if [ $? -ne 0 ] ; then fail ; fi


pass
