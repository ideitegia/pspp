#!/bin/sh

# This program tests for a bug which caused  a crash when 
# very large files are presented.


TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

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

activity="delete data"
rm -f $TEMPDIR/large.dat
if [ $? -ne 0 ] ; then no_result ; fi

printf "Creating input data.  Please wait"
activity="create data"
$PERL -e 'print "AB12\n" foreach 1...100000;
          print "AB04\n" foreach 1...100000;' > $TEMPDIR/large.dat
if [ $? -ne 0 ] ; then no_result ; fi
printf ".\n";

activity="create program"
cat > $TESTFILE <<EOF
DATA LIST FILE='$TEMPDIR/large.dat' /S 1-2 (A) X 3 .


AGGREGATE OUTFILE=* /BREAK=X /A=N.


EXAMINE /A BY /X.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 0 ] ; then fail ; fi

activity="appending to data"
# Put another 100,000 cases into large.dat
$PERL -e 'print "AB04\nAB12\n" foreach 1...25000' >> $TEMPDIR/large.dat
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 0 ] ; then fail ; fi

pass;
