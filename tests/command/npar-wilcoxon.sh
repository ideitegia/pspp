#!/bin/sh

# This program tests the wilcoxon subcommand of npar tests

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`

PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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

activity="create program 1"
cat > $TESTFILE <<  EOF
data list notable list /foo * bar * w (f8.0).
begin data.
1.00     1.00   1
1.00     2.00   1
2.00     1.00   1
1.00     4.00   1
2.00     5.00   1
1.00    19.00   1
2.00     7.00   1
4.00     5.00   1
1.00    12.00   1
2.00    13.00   1
2.00     2.00   1
12.00      .00  2
12.00     1.00  1
13.00     1.00  1
end data

variable labels foo "first" bar "second".

weight by w.

npar test
 /wilcoxon=foo with bar (paired)
 /missing analysis
 /method=exact.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="generate results"
cat > $TEMPDIR/results.csv <<EOF
Table: Ranks
,,N,Mean Rank,Sum of Ranks
second - first,Negative Ranks,5,8.60,43.00
,Positive Ranks,8,6.00,48.00
,Ties,2,,
,Total,15,,

Table: Test Statistics
,second - first
Z,-.18
Asymp. Sig. (2-tailed),.86
Exact Sig. (2-tailed),.89
Exact Sig. (1-tailed),.45
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 1"
diff -b -c pspp.csv $TEMPDIR/results.csv
if [ $? -ne 0 ] ; then fail ; fi



# No weights this time. But some missing values
activity="create program 2"
cat > $TESTFILE <<  EOF
data list notable list /foo * bar * dummy *.
begin data.
1.00     1.00    1
1.00     2.00    1
2.00     1.00    1
1.00     4.00    .
2.00     5.00    .
1.00    19.00    .
2.00     7.00    1
4.00     5.00    1
1.00    12.00    1
2.00    13.00    1
2.00     2.00    1
12.00      .00   1
12.00      .00   1
34.2       .     1
12.00     1.00   1  
13.00     1.00   1
end data

variable labels foo "first" bar "second".

npar test
 /wilcoxon=foo with bar (paired)
 /missing analysis
 /method=exact.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 2"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output 2"
diff -b pspp.csv $TEMPDIR/results.csv
if [ $? -ne 0 ] ; then fail ; fi



pass;
