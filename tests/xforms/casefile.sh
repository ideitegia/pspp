#!/bin/sh

# This program tests casefiles by running DEBUG CASEFILE.

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
cat > $TEMPDIR/casefile.stat <<EOF
DEBUG CASEFILE SMALL.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode $TEMPDIR/casefile.stat > $TEMPDIR/casefile.out
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
diff -b -B $TEMPDIR/casefile.out - <<EOF
Casefile tests succeeded.
EOF

if [ $? -ne 0 ] ; then no_result ; fi


if [ $? -ne 0 ] ; then fail ; fi



pass;
