#!/bin/sh

# This program tests use of variables in expressions.

TEMPDIR=/tmp/pspp-tst-$$

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
cat > $TEMPDIR/variables.stat <<EOF
SET MXERR 1000.
SET MXWARN 1000.

DATA LIST /N1 TO N5 1-5.
MISSING VALUES N1 TO N5 (3 THRU 5, 1).
BEGIN DATA.
12345
6789 
END DATA.

COMPUTE P1=N1.
COMPUTE P2=N2.
COMPUTE P3=N3.
COMPUTE P4=N4.
COMPUTE P5=N5.

COMPUTE MC=NMISS(N1 TO N5).
COMPUTE VC=NVALID(N1 TO N5).

COMPUTE S1=SYSMIS(N1).
COMPUTE S2=SYSMIS(N2).
COMPUTE S3=SYSMIS(N3).
COMPUTE S4=SYSMIS(N4).
COMPUTE S5=SYSMIS(N5).

COMPUTE M1=MISSING(N1).
COMPUTE M2=MISSING(N2).
COMPUTE M3=MISSING(N3).
COMPUTE M4=MISSING(N4).
COMPUTE M5=MISSING(N5).

COMPUTE V1=VALUE(N1).
COMPUTE V2=VALUE(N2).
COMPUTE V3=VALUE(N3).
COMPUTE V4=VALUE(N4).
COMPUTE V5=VALUE(N5).

FORMATS ALL (F1).

LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/variables.stat > $TEMPDIR/variables.err 2> $TEMPDIR/variables.out
if [ $? -ne 0 ] ; then fail ; fi

activity="compare results"
diff -b -B $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|N1      |     1|  1-  1|F1.0  |
|N2      |     1|  2-  2|F1.0  |
|N3      |     1|  3-  3|F1.0  |
|N4      |     1|  4-  4|F1.0  |
|N5      |     1|  5-  5|F1.0  |
+--------+------+-------+------+

N1 N2 N3 N4 N5 P1 P2 P3 P4 P5 MC VC S1 S2 S3 S4 S5 M1 M2 M3 M4 M5 V1 V2 V3 V4 V5
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
 1  2  3  4  5  .  2  .  .  .  4  1  0  0  0  0  0  1  0  1  1  1  1  2  3  4  5 
 6  7  8  9  .  6  7  8  9  .  1  4  0  0  0  0  1  0  0  0  0  1  6  7  8  9  . 
EOF

if [ $? -ne 0 ] ; then no_result ; fi


if [ $? -ne 0 ] ; then fail ; fi



pass;
