#!/bin/sh

# This program tests that DATA LIST can be used to read input files
# with varying line ends (LF only, CR LF, CR only).

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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

# Create command file.
activity="create program"
cat > $TESTFILE << EOF
data list list notable file='input.txt'/a b c.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="create input.txt"
printf '1 2 3\n4 5 6\r\n7\r8\r9\r\n10 11 12\n13 14 15 \r\n16\r\r17\r18\n' > input.txt
if [ $? -ne 0 ] ; then no_result ; fi


# Make sure that input.txt actually received the data that we expect.
# It might not have, if we're running on a system that translates \n
# into some other sequence.
activity="check input.txt"
cksum input.txt > input.cksum
diff input.cksum - <<EOF
1732021750 50 input.txt
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi


activity="compare output"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: Data List
a,b,c
1.00,2.00,3.00
4.00,5.00,6.00
7.00,8.00,9.00
10.00,11.00,12.00
13.00,14.00,15.00
16.00,17.00,18.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
