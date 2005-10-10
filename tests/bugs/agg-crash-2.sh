#!/bin/sh

# This program tests for a bug which caused AGGREGATE to crash when
# the MAX function was used.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

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
cat > $TESTFILE <<EOF
DATA LIST LIST /X (F8.2) Y (a25).

BEGIN DATA.
87.50 foo
87.34 bar
1 bar
END DATA.



AGGREGATE OUTFILE=* /BREAK=y /X=MAX(x).
LIST /x y.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

perl -pi -e s/^\s*\$//g $TEMPDIR/pspp.list
diff -b  -w $TEMPDIR/pspp.list - << EOF | perl -e 's/^\s*$//g'
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|X       |F8.2  |
|Y       |A25   |
+--------+------+

        X                         Y
--------- -------------------------
    87.34 bar                       
    87.50 foo                       
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
