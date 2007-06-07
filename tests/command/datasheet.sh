#!/bin/sh

# This program tests datasheet support.

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

activity="Create File 1"
cat > $TESTFILE <<EOF
debug datasheet max=3,3 backing=0,0/progress=none/output=file("/dev/null").
debug datasheet max=3,3 backing=3,3/progress=none/output=file("/dev/null").
debug datasheet max=3,3 backing=3,1/progress=none/output=file("/dev/null").
debug datasheet max=3,3 backing=1,3/progress=none/output=file("/dev/null").
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="Run pspp 1"
$SUPERVISOR $PSPP --testing-mode $TESTFILE > datasheet.out
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
diff -b  $TEMPDIR/datasheet.out - <<EOF
Datasheet test max(3,3) backing(0,0) successful.
Datasheet test max(3,3) backing(3,3) successful.
Datasheet test max(3,3) backing(3,1) successful.
Datasheet test max(3,3) backing(1,3) successful.
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
