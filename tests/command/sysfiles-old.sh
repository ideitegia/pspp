#!/bin/sh

# This program tests that system files can be read and written 
# without the long name tables.


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
DATA LIST LIST NOTABLE / X * variable001 * variable002 * variable003 * .
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

SAVE /OUTFILE='$TEMPDIR/foo.sav'
     /VERSION=2
     .

GET /FILE='$TEMPDIR/foo.sav'.

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="check file exists"
ls -l $TEMPDIR/foo.sav > /dev/null
if [ $? -ne 0 ] ; then no_result ; fi

# Ensure that the written file has no long name table
activity="check sysfile type"
grep  'X=X' $TEMPDIR/foo.sav
if [ $? -eq 0 ] ; then fail ; fi

activity="compare output"
perl -pi -e s/^\s*\$//g $TEMPDIR/pspp.list
diff -b  -w $TEMPDIR/pspp.list - << EOF | perl -e 's/^\s*$//g'
       X VARIABLE VARIAB_A VARIAB_B
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
