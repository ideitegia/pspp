#!/bin/sh

# This program tests the PERMISSIONS command

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
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
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/per.sps
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
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/per.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="Check Permissions"
ls -l foobar | grep '^-rwxr-xr-x'  > /dev/null
if [ $? -ne 0 ] ; then fail ; fi



pass;
