#!/bin/sh

# This program tests for a bug which caused a crash when 
# reading invalid INPUT PROGRAM syntax.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR"
     	return ; 
     fi
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


activity="create test program"
cat > $TESTFILE <<EOF 
INPUT PROGRAM.
DATA LIST /a 1-9.
BEGIN DATA
123456789
END DATA.
END INPUT PROGRAM.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


# The above syntax is invalid, so this program should fail to parse
activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 1 ] ; then fail ; fi



pass;
