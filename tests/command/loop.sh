#!/bin/sh

# This program tests the LOOP command

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

activity="create prog"
cat > $TEMPDIR/loop.stat <<EOF
data list /X 1 Y 2 ZOOLOGICAL 3.
begin data.
125
256
397
401
end data.
loop iterative_Variable=y to zoological by abs(zoological-y)/(zoological-y).
print /x iterative_Variable.
break.		/* Generates warning.
end loop.
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TEMPDIR/loop.stat > $TEMPDIR/stdout
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare stdout"
perl -pi -e 's/^\s*$//g' $TEMPDIR/stdout
diff -b $TEMPDIR/stdout  - <<EOF
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="compare results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list  - <<EOF
1.1 DATA LIST.  Reading 1 record from INLINE.
+----------+------+-------+------+
| Variable |Record|Columns|Format|
#==========#======#=======#======#
|X         |     1|  1-  1|F1.0  |
|Y         |     1|  2-  2|F1.0  |
|ZOOLOGICAL|     1|  3-  3|F1.0  |
+----------+------+-------+------+
1     2.00 
2     5.00 
3     9.00 
4      .00 
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
