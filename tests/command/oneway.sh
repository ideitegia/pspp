#!/bin/sh

# This program tests that the ONEWAY anova command works OK

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
cat > $TEMPDIR/out.stat <<EOF
data list list /id * cables * price *.
begin data.
1 2.0 3.0
2 1.0 2.0
3 2.0 4.5
4 2.0 4.5
5 3.0 6.0
end data.

ONEWAY 
 id cables BY price
 /STATISTICS descriptives homogeneity
 /MISSING analysis .
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/out.stat
if [ $? -ne 0 ] ; then no_result ; fi

pass
