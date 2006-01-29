#!/bin/sh

# This program's primary purpose is to  test the FILE HANDLE command

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

activity="create file"
cat > $TEMPDIR/wiggle.txt << EOF
1
2
5
109
EOF
if [ $? -ne 0 ] ; then no_result ; fi



activity="create program"
cat > $TESTFILE << EOF
FILE HANDLE myhandle /NAME='$TEMPDIR/wiggle.txt'.
DATA LIST LIST FILE=myhandle /x *.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
diff  -b  $TEMPDIR/pspp.list - << EOF 
1.1 DATA LIST.  Reading free-form data from myhandle.
+--------+------+
|Variable|Format|
#========#======#
|x       |F8.0  |
+--------+------+

       x
--------
    1.00 
    2.00 
    5.00 
  109.00 

EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
