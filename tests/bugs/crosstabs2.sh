#!/bin/sh

# This program tests for bug #26739, which caused CROSSTABS to crash
# or to fail to output chi-square results.

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

activity="create program"
cat > $TESTFILE <<EOF
DATA LIST LIST /x * y *.
BEGIN DATA.
2 2
3 1
4 2
4 1
END DATA.

CROSSTABS
        /TABLES= x BY y
        /STATISTICS=CHISQ
       .
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  -w $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from INLINE.
+--------+------+
|Variable|Format|
#========#======#
|x       |F8.0  |
|y       |F8.0  |
+--------+------+
2.1 CROSSTABS.  Summary.
#===============#=====================================================#
#               #                        Cases                        #
#               #-----------------+-----------------+-----------------#
#               #      Valid      |     Missing     |      Total      #
#               #--------+--------+--------+--------+--------+--------#
#               #       N| Percent|       N| Percent|       N| Percent#
#---------------#--------+--------+--------+--------+--------+--------#
#x * y          #       4|  100.0%|       0|    0.0%|       4|  100.0%#
#===============#========#========#========#========#========#========#
2.2 CROSSTABS.  x * y [count].
#===============#=================#========#
#               #        y        |        #
#               #--------+--------+        #
#              x#    1.00|    2.00|  Total #
#---------------#--------+--------+--------#
#           2.00#      .0|     1.0|     1.0#
#           3.00#     1.0|      .0|     1.0#
#           4.00#     1.0|     1.0|     2.0#
#Total          #     2.0|     2.0|     4.0#
#===============#========#========#========#
2.3 CROSSTABS.  Chi-square tests.
#===============#========#========#========#
#Statistic      #   Value|      df|  Asymp.#
#               #        |        |    Sig.#
#               #        |        |(2-sided#
#               #        |        |       )#
#---------------#--------+--------+--------#
#Pearson        #    2.00|       2|     .37#
#Chi-Square     #        |        |        #
#Likelihood     #    2.77|       2|     .25#
#Ratio          #        |        |        #
#Linear-by-Linea#     .27|       1|     .60#
#r Association  #        |        |        #
#N of Valid     #       4|        |        #
#Cases          #        |        |        #
#===============#========#========#========#
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
