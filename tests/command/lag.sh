#!/bin/sh

# This program tests the LAG function

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH

LANG=C
export LANG

cleanup()
{
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR" 
	return ; 
     fi
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
cat > $TEMPDIR/lag.stat <<EOF
data list /W 1.
begin data.
1
2
3
4
5
end data.

compute X=lag(w,1).
compute Y=lag(x).
compute Z=lag(w,2).
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/lag.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare result"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from INLINE.
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
