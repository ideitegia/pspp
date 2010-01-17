#! /bin/sh

# Tests calculation of percentiles with the 
# COMPATIBLE algorithm set.

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
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR" 
	return ; 
     fi
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


i=1;

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /X * .
BEGIN DATA.
1 
2 
3 
4 
5
END DATA.

FREQUENCIES 
	VAR=x
	/ALGORITHM=COMPATIBLE
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi

activity="run program $i"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: X
Value Label,Value,Frequency,Percent,Valid Percent,Cum Percent
,1.00,1,20.00,20.00,20.00
,2.00,1,20.00,20.00,40.00
,3.00,1,20.00,20.00,60.00
,4.00,1,20.00,20.00,80.00
,5.00,1,20.00,20.00,100.00
Total,,5,100.0,100.0,

N,Valid,5
,Missing,0
Mean,,3.00
Std Dev,,1.58
Minimum,,1.00
Maximum,,5.00
Percentiles,0,1.00
,25,1.50
,50 (Median),3.00
,75,4.50
,100,5.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
