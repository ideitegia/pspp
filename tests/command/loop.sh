#!/bin/sh

# This program tests the LOOP command

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

activity="create prog"
cat > $TEMPDIR/loop.stat <<EOF
data list /x 1 y 2 z 3.
begin data.
125
256
397
401
end data.
loop i=y to z by abs(z-y)/(z-y).
print /x i.
break.		/* Generates warning.
end loop.
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$here/../src/pspp --testing-mode -o raw-ascii $TEMPDIR/loop.stat > $TEMPDIR/stdout
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare stdout"
diff -B -b $TEMPDIR/stdout  - <<EOF
$TEMPDIR/loop.stat:10: warning: BREAK: BREAK not enclosed in DO IF structure.
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="compare results"
diff -B -b $TEMPDIR/pspp.list  - <<EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|X       |     1|  1-  1|F1.0  |
|Y       |     1|  2-  2|F1.0  |
|Z       |     1|  3-  3|F1.0  |
+--------+------+-------+------+
1     2.00 
2     5.00 
3     9.00 
4      .00 
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
