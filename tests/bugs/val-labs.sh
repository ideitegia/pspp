#!/bin/sh

# This program tests for a bug which caused VALUE LABELS to 
# crash when given invalid syntax


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

activity="create program"
cat > $TEMPDIR/rnd.sps <<EOF
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

$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/rnd.sps > /dev/null
if [ $? -ne 1 ] ; then fail ; fi

pass;
