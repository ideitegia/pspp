#!/bin/sh

# This program tests that the T-TEST /PAIRS command works OK

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


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
data list list /ID * A * B *.
begin data.
1 2.0 3.0
2 1.0 2.0
3 2.0 4.5
4 2.0 4.5
5 3.0 6.0
end data.

t-test /PAIRS a with b (PAIRED).
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -B -b $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|ID      |F8.0  |
|A       |F8.0  |
|B       |F8.0  |
+--------+------+

2.1 T-TEST.  Paired Sample Statistics
#========#====#=#==============#========#
#        #Mean|N|Std. Deviation|SE. Mean#
#========#====#=#==============#========#
#Pair 0 A#2.00|5|          .707|    .316#
#       B#4.00|5|         1.541|    .689#
#========#====#=#==============#========#

2.2 T-TEST.  Paired Samples Correlations
#======#=====#=#===========#====#
#      |     #N|Correlation|Sig.#
#======#=====#=#===========#====#
#Pair 0|A & B#5|       .918|.028#
#======#=====#=#===========#====#

2.3 T-TEST.  Paired Samples Test
#===========#=====================================================#======#==#===============#
#           #                  Paired Differences                 |      |  |               #
#           #-------+--------------+---------------+--------------+      |  |               #
#           #       |              |               |     95%      |      |  |               #
#           #       |              |               +-------+------+      |  |               #
#           #  Mean |Std. Deviation|Std. Error Mean| Lower | Upper|   t  |df|Sig. (2-tailed)#
#===========#=======#==============#===============#=======#======#======#==#===============#
#Pair 0A - B#-2.0000|        .93541|         .41833|-3.1615|-.8385|-4.781| 4|           .009#
#===========#=======#==============#===============#=======#======#======#==#===============#
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
