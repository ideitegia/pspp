#!/bin/sh

# This program tests for a bug which caused  a crash when 
# very large files are presented.


TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps
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

activity="delete data"
rm -f $TEMPDIR/large.dat
if [ $? -ne 0 ] ; then no_result ; fi

printf "Creating input data.  Please wait"
activity="create data"
( while true ; do 
	echo AB12;
done )  | head -100000 >> $TEMPDIR/large.dat
if [ $? -ne 0 ] ; then no_result ; fi
printf '.'
( while true ; do 
	echo AB04;
done )  | head -100000 >> $TEMPDIR/large.dat
if [ $? -ne 0 ] ; then no_result ; fi
printf "\n";

activity="create program"
cat > $TESTFILE <<EOF
DATA LIST FILE='$TEMPDIR/large.dat' /S 1-2 (A) X 3 .


AGGREGATE /BREAK=X /A=N.


EXAMINE /A BY /X.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 0 ] ; then fail ; fi

activity="appending to data"
# Put another 100,000 cases into large.dat
( while true ; do 
	echo AB04 
	echo AB12 
done )  | head -50000 >> $TEMPDIR/large.dat
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 0 ] ; then fail ; fi

pass;
