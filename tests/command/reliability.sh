#!/bin/sh

# This program tests the reliability command.

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

data list notable list  /var1 *
	var2  *
	var6  *
	var7  *
	var8  *
	var9  *
	var11 *
	var12 *
	var15 *
	var16 *
	var17 *
	var19 *
	.

begin data.
6 7 7 5 7 7 7 7 7 7 6 6
6 7 7 6 7 6 7 5 6 5 7 7
6 6 7 6 5 3 6 4 5 6 4 5
4 6 5 6 6 5 4 3 5 6 5 6
5 6 5 5 6 5 4 4 6 6 5 5
6 6 7 6 6 5 6 5 6 6 5 6
5 6 6 5 6 5 5 4 6 5 5 5
5 7 7 7 7 7 6 5 7 7 7 7
6 6 6 5 5 7 6 5 6 6 5 6
. . . . . . . . . . . .
6 6 5 5 5 6 6 4 6 5 5 5
7 7 7 6 7 6 7 6 6 6 7 6
4 7 6 6 6 5 5 4 4 5 5 6
5 6 3 5 4 1 4 6 2 3 3 2
3 6 6 5 6 2 4 2 2 4 4 5
6 6 7 5 6 5 7 6 5 6 6 5
6 5 6 6 5 6 6 6 6 4 5 5
5 7 7 . 6 6 6 5 6 6 6 6
5 7 5 5 4 6 7 6 5 4 6 5
7 7 7 6 7 7 7 6 7 7 7 6
3 6 5 6 5 7 7 3 4 7 5 7
6 7 7 6 5 6 5 5 6 6 6 6
5 5 6 5 5 5 5 4 5 5 5 6
6 6 7 4 5 6 6 6 6 5 5 6
6 5 6 6 4 4 5 4 5 6 4 5
5 6 7 6 6 7 7 5 6 6 6 5
5 6 5 7 4 6 6 5 7 7 5 6
. . . . . . . . . . . .
7 6 6 5 6 6 7 6 6 5 5 6
6 6 7 7 7 7 7 6 7 6 6 7
7 5 5 . 5 . 7 3 5 4 5 3
7 6 7 5 4 5 7 5 7 5 5 6
6 5 6 6 6 5 5 5 5 6 5 6
7 7 7 7 7 7 7 7 5 6 7 7
. . . . . . . . . . . .
5 5 6 7 5 6 6 4 6 6 6 5
6 6 5 7 5 6 7 5 6 5 4 6
7 6 7 6 7 5 6 7 7 6 6 6
5 6 5 6 5 6 7 2 5 7 3 7
6 6 5 6 5 6 6 6 6 6 5 6
7 6 7 6 6 6 6 6 6 7 6 7
7 7 6 5 6 6 7 7 7 4 6 5
3 7 7 6 6 7 7 7 6 6 6 4
3 5 3 4 3 3 3 3 3 3 3 5
5 7 7 7 5 7 6 2 6 7 6 7
7 7 7 7 7 7 7 6 7 7 7 6
6 5 7 4 4 4 5 6 5 5 4 5
4 7 7 4 4 3 6 3 5 3 4 5
7 7 7 7 7 7 7 7 7 7 7 5
3 6 5 5 4 5 4 4 5 5 3 5
6 7 6 6 6 7 7 6 6 6 7 6
2 5 4 6 3 2 2 2 2 7 2 2
4 6 6 5 5 5 6 5 5 6 6 5
5 7 4 5 6 6 6 5 6 6 5 6
5 7 7 5 6 5 6 5 5 4 5 4
4 5 6 5 6 4 5 5 5 4 5 5
7 6 6 5 5 6 7 5 6 5 7 6
5 6 6 5 4 5 5 3 4 5 5 5
5 7 6 4 4 5 6 5 6 4 4 6
6 6 6 6 5 7 7 6 5 5 6 6
6 6 7 6 7 6 6 5 6 7 6 5
7 6 7 6 7 6 7 7 5 5 6 6
5 6 6 5 5 5 6 5 6 7 7 5
5 6 6 5 6 5 6 6 6 6 6 6
5 5 5 5 6 4 5 3 4 7 6 5
5 7 7 6 6 6 6 5 6 7 6 7
6 6 7 7 7 5 6 5 5 5 5 4
2 7 5 4 6 5 5 2 5 6 4 6
6 7 7 5 6 6 7 6 6 7 5 7
5 6 7 6 6 3 5 7 6 6 5 6
6 6 6 3 5 5 5 6 6 6 4 5
4 7 7 4 7 4 5 5 5 7 4 4
. . . . . . . . . . . .
6 6 7 6 7 6 7 7 6 7 7 6
. . . . . . . . . . . .
5 6 5 7 6 5 6 6 5 6 4 6
5 5 5 5 4 5 5 5 7 5 5 5
6 6 6 4 5 4 6 6 6 4 5 4
6 5 7 4 6 4 6 5 6 6 6 3
5 7 6 5 5 5 5 5 6 7 6 6
5 5 7 7 5 5 6 6 5 5 5 7
5 6 7 6 7 5 6 4 6 7 6 7
4 5 5 5 6 5 6 5 6 6 5 6
6 5 5 5 6 3 4 5 5 4 5 3
6 6 6 5 5 5 4 3 4 5 5 5
6 7 7 6 2 3 6 6 6 5 7 7
6 7 5 5 6 6 6 5 6 6 6 6
6 7 7 6 7 7 7 5 5 6 6 6
6 6 6 6 7 6 6 7 6 6 6 6
5 6 6 6 3 5 6 6 5 5 4 6
4 6 5 6 6 5 6 5 6 6 5 5
6 4 6 5 4 6 7 4 5 6 5 5
6 7 6 4 6 5 7 6 7 7 6 5
6 7 7 6 7 6 7 7 7 6 6 6
6 6 6 4 5 6 7 7 5 6 4 4
3 3 5 3 3 1 5 6 3 2 3 3
7 7 5 6 6 7 7 6 7 7 7 7
5 6 6 6 7 5 4 5 4 7 6 7
3 6 5 4 3 3 3 5 5 6 3 4
5 7 6 4 6 5 5 6 6 7 5 6
5 7 6 6 6 6 6 5 6 7 7 6
7 7 5 6 7 7 7 7 6 5 7 7
6 7 6 6 5 6 7 7 6 5 6 6
6 7 7 7 7 6 6 7 6 7 7 7
4 6 4 7 3 6 5 5 4 3 5 6
5 5 7 5 4 6 7 5 4 6 6 5
5 5 6 4 6 5 7 6 5 5 5 6
. . . . . . . . . . . .
. . . . . . . . . . . .
5 7 7 5 6 6 7 7 6 6 6 7
6 7 7 1 2 1 7 7 5 5 5 2
. . . . . . . . . . . .
3 7 4 6 4 7 4 6 4 7 4 7
5 7 3 5 5 6 7 5 4 7 7 4
4 7 7 5 4 6 7 7 6 5 4 4
6 6 2 2 6 4 6 5 5 1 5 2
5 5 6 4 5 4 6 5 5 6 5 5
. . . . . . . . . . . .
5 7 6 6 6 6 6 6 5 6 6 6
6 6 6 5 6 6 6 6 7 5 6 7
3 6 3 3 5 3 3 5 3 5 7 4
4 4 6 3 3 3 4 3 4 2 3 6
5 7 7 6 5 4 7 5 7 7 3 7
4 5 4 4 4 4 3 3 3 4 3 3
6 7 7 5 6 6 7 5 4 5 5 5
3 5 3 3 1 3 4 3 4 7 6 7
4 5 4 4 4 3 4 5 6 6 4 5
5 6 3 4 5 3 5 3 4 5 6 4
5 5 5 6 6 6 6 4 5 6 6 5
6 7 7 2 2 6 7 7 7 7 5 7
5 7 7 4 6 5 7 5 5 5 6 6
6 6 7 7 5 5 5 7 6 7 7 7
6 5 7 3 6 5 6 5 5 6 5 4
5 7 6 5 6 6 6 5 6 5 5 6
4 5 5 5 6 3 5 3 3 6 5 5
. . . . . . . . . . . .
5 6 6 4 4 4 5 3 5 5 2 6
5 6 7 5 5 6 6 5 5 6 6 6
6 7 7 6 4 7 7 6 7 5 6 7
6 6 5 4 5 2 7 6 6 5 6 6
2 2 2 2 2 2 3 2 3 1 1 2
end data.

RELIABILITY
  /VARIABLES=var2 var8 var15 var17 var6
  /SCALE('Everything') var6 var8 var15 var17
  /MODEL=ALPHA.

RELIABILITY
  /VARIABLES=var6 var8 var15 var17
  /SCALE('Nothing') ALL
  /MODEL=SPLIT(2)
 .

RELIABILITY
  /VARIABLES=var2 var6 var8 var15 var17 var19
  /SCALE('Totals') var6 var8 var15 var17 
  /SUMMARY = total
 .


RELIABILITY
  /VARIABLES=var6 var8 var15 var17 
  .

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff pspp.list - << EOF
Scale: Everything

1.1 RELIABILITY.  Case Processing Summary
#==============#===#=====#
#              # N |  %  #
#==============#===#=====#
#Cases Valid   #131| 92.9#
#      Excluded# 10|  7.1#
#      Total   #141|100.0#
#==============#===#=====#

1.2 RELIABILITY.  Reliability Statistics
#================#==========#
#Cronbach's Alpha#N of items#
#================#==========#
#            .748#         4#
#================#==========#

Scale: Nothing

2.1 RELIABILITY.  Case Processing Summary
#==============#===#=====#
#              # N |  %  #
#==============#===#=====#
#Cases Valid   #131| 92.9#
#      Excluded# 10|  7.1#
#      Total   #141|100.0#
#==============#===#=====#

2.2 RELIABILITY.  Reliability Statistics
#==========================================================#====#
#Cronbach's Alpha               Part 1           Value     #.550#
#                                                N of Items#   2#
#                               Part 2           Value     #.631#
#                                                N of Items#   2#
#                               Total N of Items           #   4#
#Correlation Between Forms                                 #.606#
#Spearman-Brown Coefficient     Equal Length               #.755#
#                               Unequal Length             #.755#
#Guttman Split-Half Coefficient                            #.754#
#==========================================================#====#

Scale: Totals

3.1 RELIABILITY.  Case Processing Summary
#==============#===#=====#
#              # N |  %  #
#==============#===#=====#
#Cases Valid   #131| 92.9#
#      Excluded# 10|  7.1#
#      Total   #141|100.0#
#==============#===#=====#

3.2 RELIABILITY.  Reliability Statistics
#================#==========#
#Cronbach's Alpha#N of items#
#================#==========#
#            .748#         4#
#================#==========#

3.3 RELIABILITY.  Item-Total Statistics
#=====#==========================#==============================#================================#================================#
#     #Scale Mean if Item Deleted|Scale Variance if Item Deleted|Corrected Item-Total Correlation|Cronbach's Alpha if Item Deleted#
#=====#==========================#==============================#================================#================================#
#var6 #                    15.969|                         8.430|                            .513|                            .705#
#var8 #                    16.565|                         7.863|                            .530|                            .698#
#var15#                    16.473|                         8.451|                            .558|                            .682#
#var17#                    16.603|                         7.995|                            .570|                            .673#
#=====#==========================#==============================#================================#================================#

Scale: ANY

4.1 RELIABILITY.  Case Processing Summary
#==============#===#=====#
#              # N |  %  #
#==============#===#=====#
#Cases Valid   #131| 92.9#
#      Excluded# 10|  7.1#
#      Total   #141|100.0#
#==============#===#=====#

4.2 RELIABILITY.  Reliability Statistics
#================#==========#
#Cronbach's Alpha#N of items#
#================#==========#
#            .748#         4#
#================#==========#

EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
