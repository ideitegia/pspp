#!/bin/sh

# This program tests that tab characters can be used in string input

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

activity="create program 1"
cat > $TEMPDIR/tabs.stat <<EOF
data list /x 1-80 (a).
begin data.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 2"
printf  "\t1\t12\t123\t1234\t12345\n" >> $TEMPDIR/tabs.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="create program 3"
cat >> $TEMPDIR/tabs.stat <<EOF
end data.
print /x.
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/tabs.stat
if [ $? -ne 0 ] ; then no_result ; fi


diff -B -b $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|X       |     1|  1- 80|A80   |
+--------+------+-------+------+
    1   12  123 1234    12345
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
