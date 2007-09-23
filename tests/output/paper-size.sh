#!/bin/sh

# This program tests paper size support.

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
cat > paper-size.pspp <<EOF
debug paper size ''.
debug paper size 'a4'.
debug paper size 'letter'.
debug paper size '10x14in'.
debug paper size '210x297mm'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="Run pspp 1"
PAPERSIZE=letter $SUPERVISOR $PSPP --testing-mode paper-size.pspp > paper-size.out
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
diff -b  $TEMPDIR/paper-size.out - <<EOF
"" => 8.5 x 11.0 in, 216 x 279 mm
"a4" => 8.3 x 11.7 in, 210 x 297 mm
"letter" => 8.5 x 11.0 in, 216 x 279 mm
"10x14in" => 10.0 x 14.0 in, 254 x 356 mm
"210x297mm" => 8.3 x 11.7 in, 210 x 297 mm
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="Create File 2"
cat > paper-size-2.pspp <<EOF
debug paper size ''.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="Run pspp 2"
PAPERSIZE=a4 $SUPERVISOR $PSPP --testing-mode paper-size-2.pspp > paper-size-2.out
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results 2"
diff -b  $TEMPDIR/paper-size-2.out - <<EOF
"" => 8.3 x 11.7 in, 210 x 297 mm
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
