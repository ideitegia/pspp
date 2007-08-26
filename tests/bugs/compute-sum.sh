#!/bin/sh

# This program tests for a bug in the `compute' command, in which it
# failed to allow a newly created variable to be used as part of the
# computation, which actually makes sense for "LEAVE" variables.

TEMPDIR=/tmp/pspp-tst-$$

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

activity="copy file"
cat > compute-sum.stat <<EOF
DATA LIST /ITEM 1-3.
COMPUTE SUM=SUM+ITEM.
PRINT OUTFILE='compute-sum.out' /ITEM SUM.
LEAVE SUM
BEGIN DATA.
123
404
555
999
END DATA.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/compute-sum.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff compute-sum.out - <<EOF
 123   123.00 
 404   527.00 
 555  1082.00 
 999  2081.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
