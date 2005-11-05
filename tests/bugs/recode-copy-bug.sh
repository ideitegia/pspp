#!/bin/sh

# This program tests ....

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

activity="copy template 1" 
cp $top_srcdir/tests/bugs/recode-copy-bug-1.stat $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi

activity="copy template 2" 
cp $top_srcdir/tests/bugs/recode-copy-bug-2.stat $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi

activity="chdir"
cd $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/recode-copy-bug-1.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 1"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list $top_srcdir/tests/bugs/recode-copy-bug-1.out
diff -b -w $TEMPDIR/pspp.list $top_srcdir/tests/bugs/recode-copy-bug-1.out
if [ $? -ne 0 ] ; then fail ; fi


activity="run program 2"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/recode-copy-bug-2.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 2"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list $top_srcdir/tests/bugs/recode-copy-bug-2.out
diff -b -w $TEMPDIR/pspp.list $top_srcdir/tests/bugs/recode-copy-bug-2.out
if [ $? -ne 0 ] ; then fail ; fi

pass;
