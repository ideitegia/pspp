#!/bin/sh

# This program tests the count transformation

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

cat > $TESTFILE <<EOF
title 'Test COUNT transformation'.

* we're going to count the 2s 4s and 1s in the data
data list /V1 to V2 1-4(a).
begin data.
1234
321      <----
2 13     <----
4121
1104     ---- this is not '4', but '04' (v1 and v2 are string format )
03 4     <----
0193
end data.
count C=v1 to v2('2',' 4','1').
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|V1      |     1|  1-  2|A2    |
|V2      |     1|  3-  4|A2    |
+--------+------+-------+------+
V1 V2        C
-- -- --------
12 34      .00 
32 1      1.00 
2  13     1.00 
41 21      .00 
11 04      .00 
03  4     1.00 
01 93      .00 
EOF
if [ $? -ne 0 ] ; then no_result ; fi


pass;
