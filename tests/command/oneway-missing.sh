#!/bin/sh

# This program tests that the ONEWAY anova command works OK when there is missing data

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
DATA LIST LIST /v1 * v2 * dep * vn *.
BEGIN DATA
. .  1  4
3 3  1  2
2 2  1  2
1 1  1  2
1 1  1  2
4 4  1  2
5 5  2  2
2 2  2  2
4 4  2  2
2 2  2  2
3 3  2  2
7 7  3  2
4 4  3  2
5 5  3  2
3 3  3  2
6 6  3  2
END DATA

ONEWAY
	v1 v2 BY dep
	/STATISTICS descriptives homogeneity
	/MISSING ANALYSIS 
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="copy output"
cp $TEMPDIR/pspp.list $TEMPDIR/pspp.list1
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 2"
cat > $TESTFILE <<EOF
DATA LIST LIST /v1 * v2 * dep * vn * .
BEGIN DATA
4 .  1  2 
3 3  1  2
2 2  1  2
1 1  1  2
1 1  1  2
4 4  1  2
5 5  2  2
2 2  2  2
4 4  2  2
2 2  2  2
3 3  2  2
7 7  3  2
4 4  3  2
5 5  3  2
3 3  3  2
6 6  3  2
END DATA

ONEWAY
	v1 v2 BY dep
	/STATISTICS descriptives homogeneity
	/MISSING LISTWISE
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 2"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare outputs"
diff $TEMPDIR/pspp.list $TEMPDIR/pspp.list1
if [ $? -ne 0 ] ; then fail ; fi

# Now try a missing dependent variable
# Everything depends upon it, so it should behave as if LISTWISE were set
activity="create program 3"
cat > $TESTFILE <<EOF
DATA LIST LIST /v1 * v2 * dep * vn * .
BEGIN DATA
4 2  .  2 
3 3  1  2
2 2  1  2
1 1  1  2
1 1  1  2
4 4  1  2
5 5  2  2
2 2  2  2
4 4  2  2
2 2  2  2
3 3  2  2
7 7  3  2
4 4  3  2
5 5  3  2
3 3  3  2
6 6  3  2
END DATA

ONEWAY
	v1 v2 BY dep
	/STATISTICS descriptives homogeneity
	/MISSING ANALYSIS
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 3"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare outputs"
diff $TEMPDIR/pspp.list $TEMPDIR/pspp.list1
if [ $? -ne 0 ] ; then fail ; fi


pass
