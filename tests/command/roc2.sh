#!/bin/sh

# This program tests  the ROC command.

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

LANG=C
export LANG

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
cat > $TESTFILE <<EOF
set format F10.3.
data list notable list /x * a *.
begin data.
0 1
0 0
1 1
2 0
3 1
4 0
4 1
5 1
6 0
7 1
8 0
9 0
9 1
end data.

roc x by a (1)
	/plot none
	print = se 
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
