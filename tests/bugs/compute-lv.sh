#!/bin/sh

# This program tests for a bug involving COMPUTE and long variable names

TEMPDIR=/tmp/pspp-tst-$$
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

activity="Create prog"
cat > $TESTFILE <<EOF
DATA LIST LIST /longVariablename * x *.
BEGIN DATA.
1 2
3 4
END DATA.


COMPUTE longvariableName=100-longvariablename.

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run prog"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi


activity="compare output"
diff -B -b pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+----------------+------+
|    Variable    |Format|
#================#======#
|longVariablename|F8.0  |
|x               |F8.0  |
+----------------+------+

longVariablename        x
---------------- -------- 
           99.00     2.00
           97.00     4.00
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
