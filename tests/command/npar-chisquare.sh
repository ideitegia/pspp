#!/bin/sh

# This program tests the chisquare subcommand of the NPAR command.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`

PSPP=$top_builddir/src/ui/terminal/pspp

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

activity="create file 1"
cat <<EOF > $TESTFILE
DATA LIST NOTABLE LIST /x * y * w *.
BEGIN DATA.
1   2  1
2   1  3
3.1 1  4
3.2 2  1
4   2  2
5   3  1
1   4  2
END DATA.

WEIGHT BY w.

NPAR TESTS
  CHISQUARE=x y
  .

NPAR TESTS
  CHISQUARE=y
  /EXPECTED=3 4 5 4
  .

NPAR TESTS
  CHISQUARE=x y(2, 4)
  /EXPECTED = 6 10 3
  .

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output 1"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: x
,Observed N,Expected N,Residual
1.00,3.00,2.33,.67
2.00,3.00,2.33,.67
3.10,4.00,2.33,1.67
3.20,1.00,2.33,-1.33
4.00,2.00,2.33,-.33
5.00,1.00,2.33,-1.33
Total,14.00,,

Table: y
,Observed N,Expected N,Residual
1.00,7.00,3.50,3.50
2.00,4.00,3.50,.50
3.00,1.00,3.50,-2.50
4.00,2.00,3.50,-1.50
Total,14.00,,

Table: Test Statistics
,x,y
Chi-Square,3.14,6.00
df,5,3
Asymp. Sig.,.68,.11

Table: y
,Observed N,Expected N,Residual
1.00,7.00,2.63,4.38
2.00,4.00,3.50,.50
3.00,1.00,4.38,-3.38
4.00,2.00,3.50,-1.50
Total,14.00,,

Table: Test Statistics
,y
Chi-Square,10.61
df,3
Asymp. Sig.,.01

Table: Frequencies
,x,,,,y,,,
,Category,Observed N,Expected N,Residual,Category,Observed N,Expected N,Residual
1,2.00,3.00,3.16,-.16,2.00,4.00,2.21,1.79
2,3.00,5.00,5.26,-.26,3.00,1.00,3.68,-2.68
3,4.00,2.00,1.58,.42,4.00,2.00,1.11,.89
Total,,10.00,,,,7.00,,

Table: Test Statistics
,x,y
Chi-Square,.13,4.13
df,2,2
Asymp. Sig.,.94,.13
EOF
if [ $? -ne 0 ] ; then fail ; fi


activity="create file 2"
cat <<EOF > $TESTFILE
DATA LIST NOTABLE LIST /x * y * w *.
BEGIN DATA.
1   2  1
2   1  3
3.1 1  4
3.2 2  1
4   2  2
5   3  1
1   4  2
END DATA.

WEIGHT BY w.

NPAR TESTS
  CHISQUARE=y
  /EXPECTED = 3 4 5 4 3 1
  .
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 2"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE  > $TEMPDIR/output
if [ $? -eq 0 ] ; then no_result ; fi

activity="compare errors 2"
perl -pi -e 's/^\s*$//g' $TEMPDIR/output
diff -b  $TEMPDIR/output - << EOF
error: CHISQUARE test specified 6 expected values, but 4 distinct values were encountered in variable y.
EOF
if [ $? -ne 0 ] ; then fail ; fi


activity="create file 3"
cat <<EOF > $TESTFILE
DATA LIST NOTABLE LIST /x * y * w * .
BEGIN DATA.
1   2  1 
2   1  3
3.1 1  4
3.2 2  1
4   2  2
5   3  1
1   4  2
.   5  1
END DATA.

WEIGHT BY w.

MISSING VALUES x (4).

NPAR TESTS
  CHISQUARE=x y(-2,5)
  /MISSING=ANALYSIS
  /STATISTICS=DESCRIPTIVES
  .
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 3"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE 
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 3"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Frequencies
,x,,,,y,,,
,Category,Observed N,Expected N,Residual,Category,Observed N,Expected N,Residual
1,-2.00,.00,1.50,-1.50,-2.00,.00,1.88,-1.88
2,-1.00,.00,1.50,-1.50,-1.00,.00,1.88,-1.88
3,.00,.00,1.50,-1.50,.00,.00,1.88,-1.88
4,1.00,3.00,1.50,1.50,1.00,7.00,1.88,5.13
5,2.00,3.00,1.50,1.50,2.00,4.00,1.88,2.13
6,3.00,5.00,1.50,3.50,3.00,1.00,1.88,-.88
7,4.00,.00,1.50,-1.50,4.00,2.00,1.88,.13
8,5.00,1.00,1.50,-.50,5.00,1.00,1.88,-.88
Total,,12.00,,,,15.00,,

Table: Test Statistics
,x,y
Chi-Square,17.33,22.87
df,7,7
Asymp. Sig.,.02,.00

Table: Descriptive Statistics
,N,Mean,Std. Deviation,Minimum,Maximum
,,,,,
x,12.00,2.47,1.19,1.00,5.00
y,15.00,2.07,1.33,1.00,5.00
EOF
if [ $? -ne 0 ] ; then fail ; fi


activity="create file 4"
cat <<EOF > $TESTFILE
DATA LIST NOTABLE LIST /x * y * w * .
BEGIN DATA.
1   2  1 
2   1  3
3.1 1  4
3.2 2  1
4   2  2
5   3  1
1   4  2
.   5  1
END DATA.

WEIGHT BY w.

* MISSING VALUES x (4).

NPAR TESTS
  CHISQUARE=x y(-2,5)
  /MISSING=LISTWISE
  /STATISTICS=DESCRIPTIVES
  .
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 4"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE 
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 4"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Frequencies
,x,,,,y,,,
,Category,Observed N,Expected N,Residual,Category,Observed N,Expected N,Residual
1,-2.00,.00,1.75,-1.75,-2.00,.00,1.75,-1.75
2,-1.00,.00,1.75,-1.75,-1.00,.00,1.75,-1.75
3,.00,.00,1.75,-1.75,.00,.00,1.75,-1.75
4,1.00,3.00,1.75,1.25,1.00,7.00,1.75,5.25
5,2.00,3.00,1.75,1.25,2.00,4.00,1.75,2.25
6,3.00,5.00,1.75,3.25,3.00,1.00,1.75,-.75
7,4.00,2.00,1.75,.25,4.00,2.00,1.75,.25
8,5.00,1.00,1.75,-.75,5.00,.00,1.75,-1.75
Total,,14.00,,,,14.00,,

Table: Test Statistics
,x,y
Chi-Square,13.43,26.00
df,7,7
Asymp. Sig.,.06,.00

Table: Descriptive Statistics
,N,Mean,Std. Deviation,Minimum,Maximum
,,,,,
x,14.00,2.69,1.23,1.00,5.00
y,14.00,1.86,1.10,1.00,4.00
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
