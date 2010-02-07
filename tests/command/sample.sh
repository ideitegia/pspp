#!/bin/sh

# This program tests the SAMPLE function

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


activity="create program"
cat > $TEMPDIR/sample.stat <<EOF
set seed=3

data list notable /A 1-2.
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
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/sample.stat 
if [ $? -ne 0 ] ; then no_result ; fi

activity="create head"
grep -v '^\ *$' $TEMPDIR/pspp.csv | head -1 > $TEMPDIR/head
if [ $? -ne 0 ] ; then no_result ; fi

activity="extract data"
grep  '[0-9][0-9]*' $TEMPDIR/pspp.csv > $TEMPDIR/data
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare head"
diff -b $TEMPDIR/head - << EOF
Table: Data List
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
