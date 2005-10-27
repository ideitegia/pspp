#!/bin/sh

# This program tests for a bug which caused
# the second procedure after GET FILE to corrupt its output


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
DATA LIST LIST NOTABLE /LOCATION * EDITOR * SHELL * FREQ * .
BEGIN DATA.
    1.00     1.00    1.0     2.00
    1.00     1.00    2.0    30.00
    1.00     2.00    1.0     8.00
    1.00     2.00    2.0    20.00
    2.00     1.00    1.0     2.00
    2.00     1.00    2.0    22.00
    2.00     2.00    1.0     1.00
    2.00     2.00    2.0     3.00
END DATA.

SAVE /OUTFILE='$TEMPDIR/foo.sav'.

GET /FILE='$TEMPDIR/foo.sav'.

* This one's ok
LIST.

* But this one get rubbish
LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi




activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  -w $TEMPDIR/pspp.list - << EOF
LOCATION   EDITOR    SHELL     FREQ
 -------- -------- -------- --------
     1.00     1.00     1.00     2.00 
     1.00     1.00     2.00    30.00 
     1.00     2.00     1.00     8.00 
     1.00     2.00     2.00    20.00 
     2.00     1.00     1.00     2.00 
     2.00     1.00     2.00    22.00 
     2.00     2.00     1.00     1.00 
     2.00     2.00     2.00     3.00 
LOCATION   EDITOR    SHELL     FREQ
 -------- -------- -------- --------
     1.00     1.00     1.00     2.00 
     1.00     1.00     2.00    30.00 
     1.00     2.00     1.00     8.00 
     1.00     2.00     2.00    20.00 
     2.00     1.00     1.00     2.00 
     2.00     1.00     2.00    22.00 
     2.00     2.00     1.00     1.00 
     2.00     2.00     2.00     3.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
