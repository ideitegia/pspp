#!/bin/sh

# This program tests the ERASE command.

TEMPDIR=/tmp/pspp-tst-$$

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

activity="create file"
cat > $TEMPDIR/foobar <<EOF
xyzzy
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="check for file 1"
if [ ! -f $TEMPDIR/foobar ] ; then no_result ; fi 


activity="create program 1"
cat > $TEMPDIR/foo.sps <<EOF
set safer on

erase FILE='foobar'.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

# foobar must still exist
activity="check for file 2"
if [ ! -f $TEMPDIR/foobar ] ; then fail ; fi 

# This command must fail
activity="run prog 1"
$here/../src/pspp $TEMPDIR/foo.sps > /dev/null
if [ $? -eq 0 ] ; then fail ; fi


activity="create program 2"
cat > $TEMPDIR/foo.sps <<EOF

erase FILE='foobar'.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run prog 1"
$here/../src/pspp $TEMPDIR/foo.sps
if [ $? -ne 0 ] ; then fail ; fi

# foobar should now be gone
if [ -f $TEMPDIR/foobar ] ; then fail ; fi 






if [ $? -ne 0 ] ; then fail ; fi



pass;
