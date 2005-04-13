#!/bin/sh

# This program tests that the ONEWAY anova command works OK
# when SPLIT FILE is active

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
DATA LIST LIST /quality * brand * s *.
BEGIN DATA
3 1 1
2 1 1
1 1 1
1 1 1
4 1 1
5 2 1
2 2 1
4 2 2
2 2 2
3 2 2
7  3 2
4  3 2
5  3 2
3  3 2
6  3 2
END DATA

VARIABLE LABELS brand 'Manufacturer'.
VARIABLE LABELS quality 'Breaking Strain'.

VALUE LABELS /brand 1 'Aspeger' 2 'Bloggs' 3 'Charlies'.

SPLIT FILE by s.

ONEWAY
	quality BY brand
	/STATISTICS descriptives homogeneity
	/CONTRAST =  -2 2
	/CONTRAST = -1 1
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

diff -b -B $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|QUALITY |F8.0  |
|BRAND   |F8.0  |
|S       |F8.0  |
+--------+------+

Variable Value    Label
S            1.00

2.1 ONEWAY.  Descriptives
#===============#=======#=#====#==============#==========#=======================#=======#=======#
#               |       # |    |              |          |    95% Confidence     |       |       #
#               |       # |    |              |          +-----------+-----------+       |       #
#               |       #N|Mean|Std. Deviation|Std. Error|Lower Bound|Upper Bound|Minimum|Maximum#
#===============#=======#=#====#==============#==========#===========#===========#=======#=======#
#Breaking Strain|Aspeger#5|2.20|          1.30|       .58|        .58|       3.82|   1.00|   4.00#
#               |Bloggs #2|3.50|          2.12|      1.50|     -15.56|      22.56|   2.00|   5.00#
#               |Total  #7|2.57|          1.51|       .57|       1.17|       3.97|   1.00|   5.00#
#===============#=======#=#====#==============#==========#===========#===========#=======#=======#

2.2 ONEWAY.  Test of Homogeneity of Variances
#===============#================#===#===#============#
#               #Levene Statistic|df1|df2|Significance#
#===============#================#===#===#============#
#Breaking Strain#           1.086|  1|  5|        .345#
#===============#================#===#===#============#

2.3 ONEWAY.  ANOVA
#==============================#==============#==#===========#=====#============#
#                              #Sum of Squares|df|Mean Square|  F  |Significance#
#===============#==============#==============#==#===========#=====#============#
#Breaking Strain|Between Groups#          2.41| 1|      2.414|1.068|        .349#
#               |Within Groups #         11.30| 5|      2.260|     |            #
#               |Total         #         13.71| 6|           |     |            #
#===============#==============#==============#==#===========#=====#============#

2.4 ONEWAY.  Contrast Coefficients
#==========#==============#
#          # Manufacturer #
#          #-------+------#
#          #Aspeger|Bloggs#
#========#=#=======#======#
#Contrast|1#     -2|     2#
#        |2#     -1|     1#
#========#=#=======#======#

2.5 ONEWAY.  Contrast Tests
#===============================================#=================#==========#=====#=====#===============#
#                                       Contrast#Value of Contrast|Std. Error|  t  |  df |Sig. (2-tailed)#
#===============#======================#========#=================#==========#=====#=====#===============#
#Breaking Strain|Assume equal variances|    1   #             2.60|     2.516|1.034|    5|           .349#
#               |                      |    2   #             1.30|     1.258|1.034|    5|           .349#
#               |Does not assume equal |    1   #             2.60|     3.219| .808|1.318|           .539#
#               |                      |    2   #             1.30|     1.609| .808|1.318|           .539#
#===============#======================#========#=================#==========#=====#=====#===============#

Variable Value    Label
S            2.00

2.6 ONEWAY.  Descriptives
#===============#========#=#====#==============#==========#=======================#=======#=======#
#               |        # |    |              |          |    95% Confidence     |       |       #
#               |        # |    |              |          +-----------+-----------+       |       #
#               |        #N|Mean|Std. Deviation|Std. Error|Lower Bound|Upper Bound|Minimum|Maximum#
#===============#========#=#====#==============#==========#===========#===========#=======#=======#
#Breaking Strain|Bloggs  #3|3.00|          1.00|       .58|        .52|       5.48|   2.00|   4.00#
#               |Charlies#5|5.00|          1.58|       .71|       3.04|       6.96|   3.00|   7.00#
#               |Total   #8|4.25|          1.67|       .59|       2.85|       5.65|   2.00|   7.00#
#===============#========#=#====#==============#==========#===========#===========#=======#=======#

2.7 ONEWAY.  Test of Homogeneity of Variances
#===============#================#===#===#============#
#               #Levene Statistic|df1|df2|Significance#
#===============#================#===#===#============#
#Breaking Strain#            .923|  1|  6|        .374#
#===============#================#===#===#============#

2.8 ONEWAY.  ANOVA
#==============================#==============#==#===========#=====#============#
#                              #Sum of Squares|df|Mean Square|  F  |Significance#
#===============#==============#==============#==#===========#=====#============#
#Breaking Strain|Between Groups#          7.50| 1|      7.500|3.750|        .101#
#               |Within Groups #         12.00| 6|      2.000|     |            #
#               |Total         #         19.50| 7|           |     |            #
#===============#==============#==============#==#===========#=====#============#

2.9 ONEWAY.  Contrast Coefficients
#==========#===============#
#          #  Manufacturer #
#          #------+--------#
#          #Bloggs|Charlies#
#========#=#======#========#
#Contrast|1#    -2|       2#
#        |2#    -1|       1#
#========#=#======#========#

2.10 ONEWAY.  Contrast Tests
#===============================================#=================#==========#=====#=====#===============#
#                                       Contrast#Value of Contrast|Std. Error|  t  |  df |Sig. (2-tailed)#
#===============#======================#========#=================#==========#=====#=====#===============#
#Breaking Strain|Assume equal variances|    1   #             4.00|     2.066|1.936|    6|           .101#
#               |                      |    2   #             2.00|     1.033|1.936|    6|           .101#
#               |Does not assume equal |    1   #             4.00|     1.826|2.191|5.882|           .072#
#               |                      |    2   #             2.00|      .913|2.191|5.882|           .072#
#===============#======================#========#=================#==========#=====#=====#===============#

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
