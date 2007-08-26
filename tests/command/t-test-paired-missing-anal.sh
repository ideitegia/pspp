#!/bin/sh

# This program tests that the T-TEST /PAIRS command works OK
# when there is per ANALYSIS missing data involved.

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
data list list /id * a * b * c * d *.
begin data.
1 2.0 3.0 4.0 4.0
2 1.0 2.0 5.1 3.9
3 2.0 4.5 5.2 3.8
4 2.0 4.5 5.3 3.7
56 3.0 6.0 5.9 3.6
end data.

t-test /PAIRS a c with b d (PAIRED). 
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="copy output"
mv $TEMPDIR/pspp.list $TEMPDIR/ref.list
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 2"
cat > $TESTFILE <<EOF
data list list /id * a * b * c * d *.
begin data.
1 2.0 3.0 4.0 4.0 
2 1.0 2.0 5.1 3.9
3 2.0 4.5 5.2 3.8
4 2.0 4.5 5.3 3.7
5 3.0 6.0 5.9 .
6 3.0  .  5.9 3.6
end data.


t-test /MISSING=analysis /PAIRS a c with b d (PAIRED). 
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 2"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare outputs"
diff $TEMPDIR/ref.list $TEMPDIR/pspp.list
if [ $? -ne 0 ] ; then fail ; fi


pass
