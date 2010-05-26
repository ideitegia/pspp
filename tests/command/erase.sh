#!/bin/sh

# This program tests the ERASE command.

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

activity="create file"
cat > $TEMPDIR/foobar <<EOF
xyzzy
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="check for file 1"
if [ ! -f $TEMPDIR/foobar ] ; then no_result ; fi 


activity="create program 1"
cat > $TESTFILE <<EOF
set safer on

erase FILE='foobar'.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

# foobar must still exist
activity="check for file 2"
if [ ! -f $TEMPDIR/foobar ] ; then fail ; fi 

# This command must fail
activity="run prog 1"
$SUPERVISOR $PSPP -o pspp.csv -e /dev/null $TESTFILE 
if [ $? -eq 0 ] ; then fail ; fi


activity="create program 2"
cat > $TESTFILE <<EOF

erase FILE='foobar'.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run prog 2"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi

# foobar should now be gone
if [ -f $TEMPDIR/foobar ] ; then fail ; fi 


pass;
