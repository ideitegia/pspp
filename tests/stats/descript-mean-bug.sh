#!/bin/sh

# This program tests that DESCRIPTIVES asking for a mean only works,
# which didn't at one point.

TEMPDIR=/tmp/pspp-tst-$$

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
cat > $TEMPDIR/descript.stat <<EOF
data list notable / X 1.
begin data.
0
1
2
3
4
5
end data.

descript all/stat=mean.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/descript.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF
1.1 DESCRIPTIVES.  Valid cases = 6; cases with missing value(s) = 0.
+--------#-+-----+
|Variable#N| Mean|
#========#=#=====#
|X       #6|2.500|
+--------#-+-----+
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
