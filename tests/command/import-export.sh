#!/bin/sh

# This program tests the IMPORT and EXPORT commands

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

activity="create program"
cat > $TEMPDIR/prog.stat <<EOF
DATA LIST LIST /x * y *.
BEGIN DATA.
1 2
3 4
5 6
END DATA.

EXPORT /OUTFILE='wiz.por'.

LIST.

IMPORT /FILE='wiz.por'.

LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TEMPDIR/prog.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|X       |F8.0  |
|Y       |F8.0  |
+--------+------+

       X        Y
-------- --------
    1.00     2.00 
    3.00     4.00 
    5.00     6.00 

       X        Y
-------- --------
    1.00     2.00 
    3.00     4.00 
    5.00     6.00 

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
