#!/bin/sh

# This program tests that SYSFILE INFO works.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR"
     	return ; 
     fi
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

activity="Create test file"
cat > $TESTFILE << EOF
DATA LIST LIST /x * name (a10) .
BEGIN DATA
1 one
2 two
3 three
END DATA.
SAVE OUTFILE='pro.sav'.

sysfile info file='pro.sav'.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="filter output"
grep -v '^Created: ' $TEMPDIR/pspp.list > $TEMPDIR/out-filtered
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff $TEMPDIR/out-filtered - << EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|x       |F8.0  |
|name    |A10   |
+--------+------+

2.1 SYSFILE INFO.  
File:      pro.sav
Label:     No label.
Endian:    Little.
Variables: 2
Cases:     3
Type:      System File.
Weight:    Not weighted.
Mode:      Compression off.

+--------+-------------+---+
|Variable|Description  |Pos|
|        |             |iti|
|        |             |on |
#========#=============#===#
|x       |Format: F8.2 |  1|
+--------+-------------+---+
|name    |Format: A10  |  2|
+--------+-------------+---+

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
