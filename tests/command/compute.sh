#!/bin/sh

# This program tests the COMPUTE command
# (it also gives LPAD and RPAD a work out)

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
cat > $TEMPDIR/compute.stat <<EOF
data list /w 1-3(a).
begin data.
123
456
919
572
end data.

string z(a6).
compute x=number(w).
compute y=number(w,f8).
compute z=lpad(
	rpad(
		substr(string(x,f6),4,1),
		3,'@'),
	6,'*').
compute y=y+1e-10.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$here/../src/pspp --testing-mode -o raw-ascii $TEMPDIR/compute.stat
if [ $? -ne 0 ] ; then no_result ; fi

diff -B -b $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|W       |     1|  1-  3|A3    |
+--------+------+-------+------+

  W      Z        X        Y
--- ------ -------- --------
123 ***1@@   123.00   123.00 
456 ***4@@   456.00   456.00 
919 ***9@@   919.00   919.00 
572 ***5@@   572.00   572.00 

EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
