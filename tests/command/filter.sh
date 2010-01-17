#!/bin/sh

# This program tests ....

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


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

activity="create program"
cat > $TESTFILE << EOF
data list notable /X 1-2.
begin data.
1
2
3
4
5
6
7
8
9
10
end data.
compute FILTER_$ = mod(x,2).

filter by filter_$.
list.
filter off.
list.
compute filter_$ = 1 - filter_$.
filter by filter_$.
list.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="check results"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: Data List
X,FILTER_$
1,1.00
3,1.00
5,1.00
7,1.00
9,1.00

Table: Data List
X,FILTER_$
1,1.00
2,.00
3,1.00
4,.00
5,1.00
6,.00
7,1.00
8,.00
9,1.00
10,.00

Table: Data List
X,FILTER_$
2,1.00
4,1.00
6,1.00
8,1.00
10,1.00
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
