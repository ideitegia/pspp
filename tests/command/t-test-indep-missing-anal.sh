#!/bin/sh

# This program tests that the T-TEST /GROUPS command works OK
# when ANALYSIS missing values are involved

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
data list list /id * indep * dep1 * dep2 *.
begin data.
1  1.0 3.5 6
2  1.0 2.0 5
3  1.0 2.0 4
4  2.0 3.5 3
56 2.0 3.0 1
end data.

t-test /group=indep /var=dep1, dep2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="copy output"
cp $TEMPDIR/pspp.list $TEMPDIR/ref.list
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 2"
cat > $TESTFILE <<EOF
data list list /id * indep * dep1 * dep2.
begin data.
1 1.0 3.5 6
2 1.0 2.0 5
3 1.0 2.0 4
4 2.0 3.5 3
5 2.0 3.0 .
6 2.0 .   1
7  .  3.1 5
end data.

* Note that if the independent variable is missing, then it's implicitly 
* listwise missing.

t-test /missing=analysis /group=indep /var=dep1 dep2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 2"
$SUPERVISOR $PSPP -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare outputs"
diff $TEMPDIR/ref.list $TEMPDIR/pspp.list 
if [ $? -ne 0 ] ; then fail ; fi


pass
