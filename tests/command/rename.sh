#!/bin/sh

# This program tests that the rename command works properly

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
DATA LIST LIST /brakeFluid * y * .
BEGIN DATA.
1 3
2 3
3 3
4 3
END DATA.

LIST.

RENAME VARIABLES (brakeFluid=applecarts).

LIST.

SAVE /OUTFILE='$TEMPDIR/rename.sav'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="check sysfile"
grep -i Brake $TEMPDIR/rename.sav
if [ $? -eq 0 ] ; then fail ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading free-form data from INLINE.
+----------+------+
| Variable |Format|
#==========#======#
|brakeFluid|F8.0  |
|y         |F8.0  |
+----------+------+
brakeFluid        y
---------- --------
      1.00     3.00 
      2.00     3.00 
      3.00     3.00 
      4.00     3.00 
applecarts        y
---------- --------
      1.00     3.00 
      2.00     3.00 
      3.00     3.00 
      4.00     3.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
