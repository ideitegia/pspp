#!/bin/sh

# This program tests  the EXAMINE command.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

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
cat > $TESTFILE <<EOF
DATA LIST LIST /quality * w * brand * .
BEGIN DATA
3  1  1
2  2  1
1  2  1
1  1  1
4  1  1
4  1  1
5  1  2
2  1  2
4  4  2
2  1  2
3  1  2
7  1  3
4  2  3
5  3  3
3  1  3
6  1  3
END DATA

WEIGHT BY w.

VARIABLE LABELS brand   'Manufacturer'.
VARIABLE LABELS quality 'Breaking Strain'.

VALUE LABELS /brand 1 'Aspeger' 2 'Bloggs' 3 'Charlies'.

LIST /FORMAT=NUMBERED.

EXAMINE
	quality BY brand
	/STATISTICS descriptives extreme(3)
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

# NOTE:  In the following data: Only the extreme values have been checked
# The descriptives have been blindly pasted.
activity="compare results"
diff $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|QUALITY |F8.0  |
|W       |F8.0  |
|BRAND   |F8.0  |
+--------+------+

Case#  QUALITY        W    BRAND
----- -------- -------- --------
    1     3.00     1.00     1.00 
    2     2.00     2.00     1.00 
    3     1.00     2.00     1.00 
    4     1.00     1.00     1.00 
    5     4.00     1.00     1.00 
    6     4.00     1.00     1.00 
    7     5.00     1.00     2.00 
    8     2.00     1.00     2.00 
    9     4.00     4.00     2.00 
   10     2.00     1.00     2.00 
   11     3.00     1.00     2.00 
   12     7.00     1.00     3.00 
   13     4.00     2.00     3.00 
   14     5.00     3.00     3.00 
   15     3.00     1.00     3.00 
   16     6.00     1.00     3.00 

2.1 EXAMINE.  Case Processing Summary
#===============#===============================#
#               #             Cases             #
#               #----------+---------+----------#
#               #   Valid  | Missing |   Total  #
#               #--+-------+-+-------+--+-------#
#               # N|Percent|N|Percent| N|Percent#
#===============#==#=======#=#=======#==#=======#
#Breaking Strain#24|   100%|0|     0%|24|   100%#
#===============#==#=======#=#=======#==#=======#

2.2 EXAMINE.  Extreme Values
#=======================#===========#=====#
#                       #Case Number|Value#
#=======================#===========#=====#
#Breaking StrainHighest1#         12| 7.00#
#                      2#         16| 6.00#
#                      3#         14| 5.00#
#               --------#-----------+-----#
#                Lowest1#          4| 1.00#
#                      2#          3| 1.00#
#                      3#          3| 1.00#
#=======================#===========#=====#

2.3 EXAMINE.  Descriptives
#==========================================================#=========#==========#
#                                                          #Statistic|Std. Error#
#==========================================================#=========#==========#
#Breaking StrainMean                                       #   3.54  |   .324   #
#               95% Confidence Interval for MeanLower Bound#  3.562  |          #
#                                               Upper Bound#  3.521  |          #
#               5% Trimmed Mean                            #   3.50  |          #
#               Median                                     #   4.00  |          #
#               Variance                                   #  2.520  |          #
#               Std. Deviation                             #  1.587  |          #
#               Minimum                                    #  1.000  |          #
#               Maximum                                    #  7.000  |          #
#               Range                                      #  6.000  |          #
#               Interquartile Range                        #   2.75  |          #
#               Skewness                                   #   .059  |   .472   #
#               Kurtosis                                   #  -.358  |   .918   #
#==========================================================#=========#==========#

2.4 EXAMINE.  Case Processing Summary
#===========================#=============================#
#                           #            Cases            #
#                           #---------+---------+---------#
#                           #  Valid  | Missing |  Total  #
#                           #-+-------+-+-------+-+-------#
#               Manufacturer#N|Percent|N|Percent|N|Percent#
#===========================#=#=======#=#=======#=#=======#
#Breaking StrainAspeger     #8|   100%|0|     0%|8|   100%#
#               Bloggs      #8|   100%|0|     0%|8|   100%#
#               Charlies    #8|   100%|0|     0%|8|   100%#
#===========================#=#=======#=#=======#=#=======#

2.5 EXAMINE.  Extreme Values
#===================================#===========#=====#
#               Manufacturer        #Case Number|Value#
#===================================#===========#=====#
#Breaking StrainAspeger     Highest1#          6| 4.00#
#                                  2#          5| 4.00#
#                                  3#          1| 3.00#
#                           --------#-----------+-----#
#                            Lowest1#          4| 1.00#
#                                  2#          3| 1.00#
#                                  3#          3| 1.00#
#               --------------------#-----------+-----#
#               Bloggs      Highest1#          7| 5.00#
#                                  2#          9| 4.00#
#                                  3#          9| 4.00#
#                           --------#-----------+-----#
#                            Lowest1#         10| 2.00#
#                                  2#          8| 2.00#
#                                  3#         11| 3.00#
#               --------------------#-----------+-----#
#               Charlies    Highest1#         12| 7.00#
#                                  2#         16| 6.00#
#                                  3#         14| 5.00#
#                           --------#-----------+-----#
#                            Lowest1#         15| 3.00#
#                                  2#         13| 4.00#
#                                  3#         13| 4.00#
#===================================#===========#=====#

2.6 EXAMINE.  Descriptives
#======================================================================#=========#==========#
#               Manufacturer                                           #Statistic|Std. Error#
#======================================================================#=========#==========#
#Breaking StrainAspeger     Mean                                       #   2.25  |   .453   #
#                           95% Confidence Interval for MeanLower Bound#  2.279  |          #
#                                                           Upper Bound#  2.221  |          #
#                           5% Trimmed Mean                            #   2.22  |          #
#                           Median                                     #   2.00  |          #
#                           Variance                                   #  1.643  |          #
#                           Std. Deviation                             #  1.282  |          #
#                           Minimum                                    #  1.000  |          #
#                           Maximum                                    #  4.000  |          #
#                           Range                                      #  3.000  |          #
#                           Interquartile Range                        #   2.75  |          #
#                           Skewness                                   #   .475  |   .752   #
#                           Kurtosis                                   #  -1.546 |   1.481  #
#               -------------------------------------------------------#---------+----------#
#               Bloggs      Mean                                       #   3.50  |   .378   #
#                           95% Confidence Interval for MeanLower Bound#  3.525  |          #
#                                                           Upper Bound#  3.475  |          #
#                           5% Trimmed Mean                            #   3.50  |          #
#                           Median                                     #   4.00  |          #
#                           Variance                                   #  1.143  |          #
#                           Std. Deviation                             #  1.069  |          #
#                           Minimum                                    #  2.000  |          #
#                           Maximum                                    #  5.000  |          #
#                           Range                                      #  3.000  |          #
#                           Interquartile Range                        #   1.75  |          #
#                           Skewness                                   #  -.468  |   .752   #
#                           Kurtosis                                   #  -.831  |   1.481  #
#               -------------------------------------------------------#---------+----------#
#               Charlies    Mean                                       #   4.88  |   .441   #
#                           95% Confidence Interval for MeanLower Bound#  4.904  |          #
#                                                           Upper Bound#  4.846  |          #
#                           5% Trimmed Mean                            #   4.86  |          #
#                           Median                                     #   5.00  |          #
#                           Variance                                   #  1.554  |          #
#                           Std. Deviation                             #  1.246  |          #
#                           Minimum                                    #  3.000  |          #
#                           Maximum                                    #  7.000  |          #
#                           Range                                      #  4.000  |          #
#                           Interquartile Range                        #   1.75  |          #
#                           Skewness                                   #   .304  |   .752   #
#                           Kurtosis                                   #   .146  |   1.481  #
#======================================================================#=========#==========#

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
