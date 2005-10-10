#!/bin/sh

# This program tests the IMPORT and EXPORT commands

TEMPDIR=/tmp/pspp-tst-$$
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

activity="create program"
cat > $TESTFILE <<EOF
DATA LIST LIST NOTABLE /X Y.
BEGIN DATA.
1 2
3 4
5 6
END DATA.

EXPORT /OUTFILE='wiz.por'.
IMPORT /FILE='wiz.por'.

LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e s/^\s*\$//g $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF | perl -e 's/^\s*$//g'
       X        Y
-------- --------
    1.00     2.00 
    3.00     4.00 
    5.00     6.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
