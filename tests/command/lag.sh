#!/bin/sh

# This program tests the LAG function

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
cat > $TEMPDIR/lag.stat <<EOF
data list /w 1.
begin data.
1
2
3
4
5
end data.

compute x=lag(w,1).
compute y=lag(x).
compute z=lag(w,2).
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/lag.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare result"
diff -b -B $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|W       |     1|  1-  1|F1.0  |
+--------+------+-------+------+

W        X        Y        Z
- -------- -------- --------
1      .        .        .   
2     1.00      .        .   
3     2.00     1.00     1.00 
4     3.00     2.00     2.00 
5     4.00     3.00     3.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
