#!/bin/sh

# This program tests the flip command

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

activity="create flip.stat"
cat > $TEMPDIR/flip.stat <<EOF
data list /N 1 (a) A B C D 2-9.
list.
begin data.
v 1 2 3 4 5
w 6 7 8 910
x1112131415
y1617181920
z2122232425
end data.
flip newnames=n.
list.
flip.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/flip.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e s/^\s*\$//g $TEMPDIR/pspp.list
diff  -b  $TEMPDIR/pspp.list - << EOF | perl -e 's/^\s*$//g'
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|N       |     1|  1-  1|A1    |
|A       |     1|  2-  3|F2.0  |
|B       |     1|  4-  5|F2.0  |
|C       |     1|  6-  7|F2.0  |
|D       |     1|  8-  9|F2.0  |
+--------+------+-------+------+

N  A  B  C  D
- -- -- -- --
v  1  2  3  4 
w  6  7  8  9 
x 11 12 13 14 
y 16 17 18 19 
z 21 22 23 24 

CASE_LBL        V        W        X        Y        Z
-------- -------- -------- -------- -------- --------
A            1.00     6.00    11.00    16.00    21.00 
B            2.00     7.00    12.00    17.00    22.00 
C            3.00     8.00    13.00    18.00    23.00 
D            4.00     9.00    14.00    19.00    24.00 

CASE_LBL        A        B        C        D
-------- -------- -------- -------- --------
V            1.00     2.00     3.00     4.00 
W            6.00     7.00     8.00     9.00 
X           11.00    12.00    13.00    14.00 
Y           16.00    17.00    18.00    19.00 
Z           21.00    22.00    23.00    24.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
