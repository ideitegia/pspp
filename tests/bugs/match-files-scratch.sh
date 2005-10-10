#!/bin/sh

# This program tests for a bug which caused MATCH FILES to crash
# when used with scratch variables.


TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.pspp

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
DATA LIST LIST /w * x * y * .
BEGIN DATA
4 5 6
1 2 3
END DATA.

COMPUTE j=0.
LOOP #k = 1 to 10.
COMPUTE j=#k + j.
END LOOP.

MATCH FILES FILE=* /DROP=w.
LIST.
FINISH.

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
|w       |F8.0  |
|x       |F8.0  |
|y       |F8.0  |
+--------+------+

       x        y        j
-------- -------- --------
    5.00     6.00    55.00 
    2.00     3.00    55.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
