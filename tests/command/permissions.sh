#!/bin/sh

# This program tests the PERMISSIONS command

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


activity="Create file"
echo HEllo > foobar
chmod 777 foobar
if [ $? -ne 0 ] ; then no_result ; fi

activity="Create program"
cat > per.sps <<EOF
PERMISSIONS /FILE='foobar'
   PERMISSIONS = READONLY.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/per.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="Check Permissions"
ls -l foobar | grep '^-r-xr-xr-x'  > /dev/null
if [ $? -ne 0 ] ; then fail ; fi


activity="Create program"
cat > per.sps <<EOF
PERMISSIONS /FILE='foobar'
   PERMISSIONS = WRITEABLE.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/per.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="Check Permissions"
ls -l foobar | grep '^-rwxr-xr-x'  > /dev/null
if [ $? -ne 0 ] ; then fail ; fi



pass;
