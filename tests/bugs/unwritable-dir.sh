#!/bin/sh

# This program tests for a bug which crashed pspp when output drivers
# tried to create output files in an unwritable directory.

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
     chmod u+w $TEMPDIR
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

activity="create test syntax"
cat > test.pspp <<EOF
* By including at least one command in the input, we can ensure that PSPP
  also doesn't crash trying to create pspp.jnl in an unwritable directory.

* By outputting a chart, we can check that charts are safe too.

data list /x 1.
begin data.
1
2
3
end data.
frequencies x/histogram.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

# Mark our directory unwritable.
chmod u-w $TEMPDIR

# Iterate over the various drivers we support.  We could use all of these
# on a single PSPP invocation, except that charts are only supported for
# a single driver at a time, and we'd prefer to test chart support for
# all of our driver types.
for driver in list-ascii list-ps html; do
    # PSPP will fail to create the output file.  Currently this doesn't cause
    # PSPP's exit status to be nonzero, although this is arguably incorrect.
    # At any rate, PSPP should not crash.
    activity="run pspp with $driver driver"
    $SUPERVISOR $PSPP -o $driver test.pspp >/dev/null 2>&1
    if [ $? -ne 0 ] ; then fail ; fi
done

pass;
