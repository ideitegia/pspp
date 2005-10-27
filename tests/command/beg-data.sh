#!/bin/sh

# This program tests the BEGIN DATA / END DATA commands

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
cat > $TESTFILE << EOF_foobar
title 'Test BEGIN DATA ... END DATA'.

data list /A B 1-2.
list.
begin data.
12
34
56
78
90
end data.

data list /A B 1-2.
begin data.
09
87
65
43
21
end data.
list.
EOF_foobar
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare data"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b $TEMPDIR/pspp.list - << foobar
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|A       |     1|  1-  1|F1.0  |
|B       |     1|  2-  2|F1.0  |
+--------+------+-------+------+
A B
- -
1 2 
3 4 
5 6 
7 8 
9 0 
2.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|A       |     1|  1-  1|F1.0  |
|B       |     1|  2-  2|F1.0  |
+--------+------+-------+------+
A B
- -
0 9 
8 7 
6 5 
4 3 
2 1 
foobar
if [ $? -ne 0 ] ; then fail ; fi


pass;
