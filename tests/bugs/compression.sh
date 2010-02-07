#!/bin/sh

# This program tests that compressed system files can be read and written

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

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

cat > $TESTFILE <<EOF
DATA LIST LIST /x * y (a200).
BEGIN DATA.
1.2 xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
.   yyyyyyyyyyyyyyy
0   ddddddddddddddddddddddddddddddd
101 z
END DATA.

SAVE OUTFILE='$TEMPDIR/com.sav' /COMPRESS .

GET FILE='$TEMPDIR/com.sav'.

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi

# Make sure the file really was compressed
activity="inspect system file"
dd if=$TEMPDIR/com.sav bs=1 skip=72 count=4 2> /dev/null | od > $TEMPDIR/file
if [ $? -ne 0 ] ; then no_result ; fi

activity="check compression setting"
# Big-endian?
diff -b $TEMPDIR/file - > /dev/null <<EOF
0000000 000000 000001
0000004
EOF
if [ $? -ne 0 ] ; then pass ; fi
# Little-endian?
diff -b $TEMPDIR/file - > /dev/null <<EOF
0000000 000001 000000
0000004
EOF
if [ $? -ne 0 ] ; then pass ; fi
# Otherwise error.
cat $TEMPDIR/file
fail
