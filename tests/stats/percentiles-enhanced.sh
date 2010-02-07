#! /bin/sh

# Tests calculation of percentiles with the 
# ENHANCED algorithm set.

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
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi

activity="run program $i"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/prog.sps
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
,25,2.00
,50 (Median),3.00
,75,4.00
,100,5.00
EOF
if [ $? -ne 0 ] ; then fail ; fi



i=$[$i+1];

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /X * F *.
BEGIN DATA.
1 2
2 2
3 2
4 1
4 1
5 1
5 1
END DATA.

WEIGHT BY f.

FREQUENCIES 
	VAR=x
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi


activity="run program $i"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: X
Value Label,Value,Frequency,Percent,Valid Percent,Cum Percent
,1.00,2.00,20.00,20.00,20.00
,2.00,2.00,20.00,20.00,40.00
,3.00,2.00,20.00,20.00,60.00
,4.00,2.00,20.00,20.00,80.00
,5.00,2.00,20.00,20.00,100.00
Total,,10.00,100.0,100.0,

N,Valid,10.00
,Missing,.00
Mean,,3.00
Std Dev,,1.49
Minimum,,1.00
Maximum,,5.00
Percentiles,0,1.00
,25,2.00
,50 (Median),3.00
,75,4.00
,100,5.00
EOF
if [ $? -ne 0 ] ; then fail ; fi



i=$[$i+1];

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /X * F *.
BEGIN DATA.
1 1
3 2
4 1
5 1
5 1
END DATA.

WEIGHT BY f.

FREQUENCIES 
	VAR=x
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi


activity="run program $i"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: X
Value Label,Value,Frequency,Percent,Valid Percent,Cum Percent
,1.00,1.00,16.67,16.67,16.67
,3.00,2.00,33.33,33.33,50.00
,4.00,1.00,16.67,16.67,66.67
,5.00,2.00,33.33,33.33,100.00
Total,,6.00,100.0,100.0,

N,Valid,6.00
,Missing,.00
Mean,,3.50
Std Dev,,1.52
Minimum,,1.00
Maximum,,5.00
Percentiles,0,1.00
,25,3.00
,50 (Median),3.50
,75,4.75
,100,5.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

i=$[$i+1];

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /X * F *.
BEGIN DATA.
1 1
3 2
4 1
5 1
5 1
99 4
END DATA.

MISSING VALUE x (99.0) .
WEIGHT BY f.

FREQUENCIES 
	VAR=x
	/PERCENTILES = 0 25 50 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi


activity="run program $i"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output $i"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: X
Value Label,Value,Frequency,Percent,Valid Percent,Cum Percent
,1.00,1.00,10.00,16.67,16.67
,3.00,2.00,20.00,33.33,50.00
,4.00,1.00,10.00,16.67,66.67
,5.00,2.00,20.00,33.33,100.00
,99.00,4.00,40.00,Missing,
Total,,10.00,100.0,100.0,

N,Valid,6.00
,Missing,4.00
Mean,,3.50
Std Dev,,1.52
Minimum,,1.00
Maximum,,5.00
Percentiles,0,1.00
,25,3.00
,50 (Median),3.50
,75,4.75
,100,5.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
