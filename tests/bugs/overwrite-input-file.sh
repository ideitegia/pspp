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

activity="create data file"
cat > foo.data <<EOF
1
2
3
4
5
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 1"
cat > $TESTFILE <<EOF
DATA LIST FILE='foo.data' /X 1.
SAVE OUTFILE='foo.sav'.
EXPORT OUTFILE='foo.por'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 1"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="check and save copy of output files"
# Check that the files are nonzero length.
test -s foo.data || fail
test -s foo.sav || fail
test -s foo.por || fail
# Save copies of them.
cp foo.data foo.data.backup || fail
cp foo.sav foo.sav.backup || fail
cp foo.por foo.por.backup || fail


activity="create program 2"
cat > $TESTFILE <<EOF
GET 'foo.sav'.
COMPUTE Y = X + 1.
XSAVE OUTFILE='foo.sav'.
XEXPORT OUTFILE='foo.por'.
PRINT OUTFILE='foo.data'.
HOST kill -TERM \$PPID
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 2"
{ $SUPERVISOR $PSPP --testing-mode $TESTFILE -e /dev/null; } >/dev/null 2>&1
# PSPP should have terminated with a signal.  POSIX requires that the exit
# status of a process terminated by a signal be greater than 128.
if [ $? -le 128 ] ; then no_result ; fi

activity="check for remaining temporary files"
if test -e *.tmp*; then fail; fi

activity="compare output 1"
cmp foo.sav foo.sav.backup
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output 2"
cmp foo.por foo.por.backup
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output 3"
cmp foo.data foo.data.backup
if [ $? -ne 0 ] ; then fail ; fi


activity="create program 3"
cat > $TESTFILE <<EOF
DATA LIST NOTABLE LIST FILE='foo.data'/X.
COMPUTE Y = X + 1.
PRINT OUTFILE='foo.data'/X Y.
EXECUTE.

GET 'foo.sav'.
COMPUTE Y = X + 2.
SAVE OUTFILE='foo.sav'.

IMPORT 'foo.por'.
COMPUTE Y = X + 3.
EXPORT OUTFILE='foo.por'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 3"
$SUPERVISOR $PSPP --testing-mode $TESTFILE -e /dev/null
if [ $? -ne 0 ] ; then no_result ; fi

activity="check for remaining temporary files"
if test -e *.tmp*; then fail; fi

activity="create program 4"
cat > $TESTFILE <<EOF
DATA LIST LIST NOTABLE FILE='foo.data'/X Y.
LIST.

GET 'foo.sav'.
LIST.

IMPORT 'foo.por'.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 4"
$SUPERVISOR $PSPP --testing-mode $TESTFILE -e /dev/null
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b pspp.list - << EOF
       X        Y
-------- --------
    1.00     2.00
    2.00     3.00
    3.00     4.00
    4.00     5.00
    5.00     6.00
X        Y
- --------
1     3.00
2     4.00
3     5.00
4     6.00
5     7.00
X        Y
- --------
1     4.00
2     5.00
3     6.00
4     7.00
5     8.00
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
