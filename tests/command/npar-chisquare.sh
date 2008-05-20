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
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output 1"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b $TEMPDIR/pspp.list - << EOF
1.1 NPAR TESTS.  x
+--------#----------+----------+--------+
|        #Observed N|Expected N|Residual|
+--------#----------+----------+--------+
|    1.00#         3|      2.33|     .67|
|    2.00#         3|      2.33|     .67|
|    3.10#         4|      2.33|    1.67|
|    3.20#         1|      2.33|   -1.33|
|    4.00#         2|      2.33|    -.33|
|    5.00#         1|      2.33|   -1.33|
|Total   #        14|          |        |
+--------#----------+----------+--------+
1.2 NPAR TESTS.  y
+--------#----------+----------+--------+
|        #Observed N|Expected N|Residual|
+--------#----------+----------+--------+
|    1.00#         7|      3.50|    3.50|
|    2.00#         4|      3.50|     .50|
|    3.00#         1|      3.50|   -2.50|
|    4.00#         2|      3.50|   -1.50|
|Total   #        14|          |        |
+--------#----------+----------+--------+
1.3 NPAR TESTS.  Test Statistics
+-----------#-----+-----+
|           #  x  |  y  |
+-----------#-----+-----+
|Chi-Square #3.143|6.000|
|df         #    5|    3|
|Asymp. Sig.# .678| .112|
+-----------#-----+-----+
2.1 NPAR TESTS.  y
+--------#----------+----------+--------+
|        #Observed N|Expected N|Residual|
+--------#----------+----------+--------+
|    1.00#         7|      2.63|    4.38|
|    2.00#         4|      3.50|     .50|
|    3.00#         1|      4.38|   -3.38|
|    4.00#         2|      3.50|   -1.50|
|Total   #        14|          |        |
+--------#----------+----------+--------+
2.2 NPAR TESTS.  Test Statistics
+-----------#------+
|           #   y  |
+-----------#------+
|Chi-Square #10.610|
|df         #     3|
|Asymp. Sig.#  .014|
+-----------#------+
3.1 NPAR TESTS.  Frequencies
+-----#---------------------------------------#---------------------------------------+
|     #                   x                   #                   y                   |
|     #--------+----------+----------+--------#--------+----------+----------+--------+
|     #Category|Observed N|Expected N|Residual#Category|Observed N|Expected N|Residual|
+-----#--------+----------+----------+--------#--------+----------+----------+--------+
|1    #    2.00|         3|      3.16|    -.16#    2.00|         4|      2.21|    1.79|
|2    #    3.00|         5|      5.26|    -.26#    3.00|         1|      3.68|   -2.68|
|3    #    4.00|         2|      1.58|     .42#    4.00|         2|      1.11|     .89|
|Total#        |        10|          |        #        |         7|          |        |
+-----#--------+----------+----------+--------#--------+----------+----------+--------+
3.2 NPAR TESTS.  Test Statistics
+-----------#----+-----+
|           #  x |  y  |
+-----------#----+-----+
|Chi-Square #.133|4.129|
|df         #   2|    2|
|Asymp. Sig.#.936| .127|
+-----------#----+-----+
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
$SUPERVISOR $PSPP --testing-mode $TESTFILE  > $TEMPDIR/output
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
$SUPERVISOR $PSPP --testing-mode $TESTFILE 
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 3"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b $TEMPDIR/pspp.list - <<EOF
1.1 NPAR TESTS.  Frequencies
+-----#---------------------------------------#---------------------------------------+
|     #                   x                   #                   y                   |
|     #--------+----------+----------+--------#--------+----------+----------+--------+
|     #Category|Observed N|Expected N|Residual#Category|Observed N|Expected N|Residual|
+-----#--------+----------+----------+--------#--------+----------+----------+--------+
|1    #   -2.00|         0|      1.50|   -1.50#   -2.00|         0|      1.88|   -1.88|
|2    #   -1.00|         0|      1.50|   -1.50#   -1.00|         0|      1.88|   -1.88|
|3    #     .00|         0|      1.50|   -1.50#     .00|         0|      1.88|   -1.88|
|4    #    1.00|         3|      1.50|    1.50#    1.00|         7|      1.88|    5.13|
|5    #    2.00|         3|      1.50|    1.50#    2.00|         4|      1.88|    2.13|
|6    #    3.00|         5|      1.50|    3.50#    3.00|         1|      1.88|    -.88|
|7    #    4.00|         0|      1.50|   -1.50#    4.00|         2|      1.88|     .13|
|8    #    5.00|         1|      1.50|    -.50#    5.00|         1|      1.88|    -.88|
|Total#        |        12|          |        #        |        15|          |        |
+-----#--------+----------+----------+--------#--------+----------+----------+--------+
1.2 NPAR TESTS.  Test Statistics
+-----------#------+------+
|           #   x  |   y  |
+-----------#------+------+
|Chi-Square #17.333|22.867|
|df         #     7|     7|
|Asymp. Sig.#  .015|  .002|
+-----------#------+------+
1.3 NPAR TESTS.  Descriptive Statistics
+-#--+-----+-----+-----+-----+
| # N| Mean| Std.|Minim|Maxim|
| #  |     |Devia|  um |  um |
#=#==#=====#=====#=====#=====#
|x#12|2.467|1.193|1.000|5.000|
|y#15|2.067|1.335|1.000|5.000|
+-#--+-----+-----+-----+-----+
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
$SUPERVISOR $PSPP --testing-mode $TESTFILE 
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 4"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b $TEMPDIR/pspp.list - <<EOF
1.1 NPAR TESTS.  Frequencies
+-----#---------------------------------------#---------------------------------------+
|     #                   x                   #                   y                   |
|     #--------+----------+----------+--------#--------+----------+----------+--------+
|     #Category|Observed N|Expected N|Residual#Category|Observed N|Expected N|Residual|
+-----#--------+----------+----------+--------#--------+----------+----------+--------+
|1    #   -2.00|         0|      1.75|   -1.75#   -2.00|         0|      1.75|   -1.75|
|2    #   -1.00|         0|      1.75|   -1.75#   -1.00|         0|      1.75|   -1.75|
|3    #     .00|         0|      1.75|   -1.75#     .00|         0|      1.75|   -1.75|
|4    #    1.00|         3|      1.75|    1.25#    1.00|         7|      1.75|    5.25|
|5    #    2.00|         3|      1.75|    1.25#    2.00|         4|      1.75|    2.25|
|6    #    3.00|         5|      1.75|    3.25#    3.00|         1|      1.75|    -.75|
|7    #    4.00|         2|      1.75|     .25#    4.00|         2|      1.75|     .25|
|8    #    5.00|         1|      1.75|    -.75#    5.00|         0|      1.75|   -1.75|
|Total#        |        14|          |        #        |        14|          |        |
+-----#--------+----------+----------+--------#--------+----------+----------+--------+
1.2 NPAR TESTS.  Test Statistics
+-----------#------+------+
|           #   x  |   y  |
+-----------#------+------+
|Chi-Square #13.429|26.000|
|df         #     7|     7|
|Asymp. Sig.#  .062|  .001|
+-----------#------+------+
1.3 NPAR TESTS.  Descriptive Statistics
+-#--+-----+-----+-----+-----+
| # N| Mean| Std.|Minim|Maxim|
| #  |     |Devia|  um |  um |
#=#==#=====#=====#=====#=====#
|x#14|2.686|1.231|1.000|5.000|
|y#14|1.857|1.099|1.000|4.000|
+-#--+-----+-----+-----+-----+
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
