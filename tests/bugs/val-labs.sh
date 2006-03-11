#!/bin/sh

# This program tests for a bug which caused VALUE LABELS to 
# crash when given invalid syntax


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
DATA LIST LIST /a * pref * .
BEGIN DATA.
    1.00     1.00    
    1.00     2.00    
    2.00     1.00    
    2.00     2.00    
END DATA.

VALUE LABELS /var=a 'label for a'.



EOF
if [ $? -ne 0 ] ; then no_result ; fi

#Invalid syntax --- return value is non zero.
activity="run program"
$SUPERVISOR $PSPP -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 1 ] ; then fail ; fi

pass;
