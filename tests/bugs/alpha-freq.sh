#!/bin/sh

# This program tests for a bug where the FREQUENCIES command would 
# crash if given an alphanumeric variable

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


$SUPERVISOR $here/../src/pspp $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then fail ; fi


pass;
