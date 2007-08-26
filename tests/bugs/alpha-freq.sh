#!/bin/sh

# This program tests for a bug where the FREQUENCIES command would 
# crash if given an alphanumeric variable

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

cat > $TEMPDIR/prog.sps <<EOF
DATA LIST FREE/
   name  (A8) value * quantity .
BEGIN DATA.
Cables 829 3 
END DATA.
EXECUTE.

FREQUENCIES /VAR = name.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


$SUPERVISOR $PSPP --testing-mode $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then fail ; fi


pass;
