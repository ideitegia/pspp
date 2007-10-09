#!/bin/sh

# This program tests for a bug that caused SAVE to the file currently
# being read with GET to truncate the save file to zero length, and
# similarly for IMPORT/EXPORT.


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
DATA LIST /X 1.
BEGIN DATA.
1
2
3
4
5
END DATA.

SAVE OUTFILE='foo.sav'.
EXPORT OUTFILE='foo.por'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="check and save copy of output files"
# Check that the files are nonzero length.
test -s foo.sav || fail
test -s foo.por || fail
# Save copies of them.
cp foo.sav foo.sav.backup || fail
cp foo.por foo.por.backup || fail


activity="create program 2"
cat > $TESTFILE <<EOF
GET 'foo.sav'.
SAVE OUTFILE='foo.sav'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 2"
$SUPERVISOR $PSPP --testing-mode $TESTFILE -e /dev/null
# This should have failed with an error message.
if [ $? -eq 0 ] ; then no_result ; fi


activity="create program 3"
cat > $TESTFILE <<EOF
IMPORT 'foo.por'.
EXPORT OUTFILE='foo.por'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 3"
$SUPERVISOR $PSPP --testing-mode $TESTFILE -e /dev/null
# This should have failed with an error message.
if [ $? -eq 0 ] ; then no_result ; fi


activity="compare output 1"
cmp foo.sav foo.sav.backup
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output 2"
cmp foo.por foo.por.backup
if [ $? -ne 0 ] ; then fail ; fi

pass;
