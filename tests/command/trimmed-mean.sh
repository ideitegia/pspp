#!/bin/sh

# This program tests  the Trimmed Mean calculation, in the case
# where the data is weighted towards the centre

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
DATA LIST LIST /X * C *.
BEGIN DATA.
1 1
2 49
3 2
END DATA.

WEIGHT BY c.

EXAMINE
	x
	/STATISTICS=DESCRIPTIVES
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from INLINE.
+--------+------+
|Variable|Format|
#========#======#
|X       |F8.0  |
|C       |F8.0  |
+--------+------+
2.1 EXAMINE.  Case Processing Summary
#=#===============================#
# #             Cases             #
# #----------+---------+----------#
# #   Valid  | Missing |   Total  #
# #--+-------+-+-------+--+-------#
# # N|Percent|N|Percent| N|Percent#
#=#==#=======#=#=======#==#=======#
#X#52|   100%|0|     0%|52|   100%#
#=#==#=======#=#=======#==#=======#
2.2 EXAMINE.  Descriptives
#==============================================#=========#==========#
#                                              #Statistic|Std. Error#
#==============================================#=========#==========#
#X Mean                                        #   2.02  |   .034   #
#  95% Confidence Interval for Mean Lower Bound#  1.952  |          #
#                                   Upper Bound#  2.087  |          #
#  5% Trimmed Mean                             #   2.00  |          #
#  Median                                      #   2.00  |          #
#  Variance                                    #   .058  |          #
#  Std. Deviation                              #   .242  |          #
#  Minimum                                     #  1.000  |          #
#  Maximum                                     #  3.000  |          #
#  Range                                       #  2.000  |          #
#  Interquartile Range                         #   .00   |          #
#  Skewness                                    #  1.194  |   .330   #
#  Kurtosis                                    #  15.732 |   .650   #
#==============================================#=========#==========#
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
