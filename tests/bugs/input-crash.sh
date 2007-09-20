#!/bin/sh

# This program tests for a bug which caused a crash when 
# reading invalid INPUT PROGRAM syntax.

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
$SUPERVISOR $PSPP --testing-mode -e /dev/null $TESTFILE 
if [ $? -ne 1 ] ; then fail ; fi


activity="create test program 2"
cat > $TESTFILE <<EOF
* From bug #21108.
input program.
data list list /x.
end file.
end input program.

descriptives x.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


# The above syntax is invalid, so this program should fail to parse
activity="run program 2"
$SUPERVISOR $PSPP --testing-mode -e /dev/null $TESTFILE
if [ $? -ne 1 ] ; then fail ; fi


pass;
