#!/bin/sh

# This program tests that simultaneous input and output to a single
# file properly coexist, with the output atomically replacing the
# input if successful.

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

# This test only works on systems that have a /dev/null device...
test -c /dev/null || no_result

# ...and that are able to make a symbolic link to it...
ln -s /dev/null foo.out || no_result

# ...that is still /dev/null.
test -c /dev/null || no_result

activity="create program 1"
cat > $TESTFILE <<EOF
DATA LIST /x 1.
BEGIN DATA.
1
2
3
4
5
END DATA.
PRINT OUTFILE='foo.out'/x.
PRINT OUTFILE='foo2.out'/x.
EXECUTE.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 1"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="check that foo2.out was created"
diff -b foo2.out - << EOF
 1
 2
 3
 4
 5
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="check that foo.out is unchanged"
test -c /dev/null || fail

pass;
