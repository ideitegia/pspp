#!/bin/sh

# This program tests that the T-TEST /GROUPS command works

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
data list list /ID * INDEP * DEP1 * DEP2 *.
begin data.
1  1.1 1 3
2  1.1 2 4
3  1.1 2 4 
4  1.1 2 4 
5  1.1 3 5
6  2.1 3 1
7  2.1 4 2
8  2.1 4 2
9  2.1 4 2
10 2.1 5 3
11 3.1 2 2
end data.

* Note that this last case should be IGNORED since it doesn't have a dependent variable of either 1 or 2

t-test /GROUPS=indep(1.1,2.1) /var=dep1 dep2.
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
|INDEP   |F8.0  |
|DEP1    |F8.0  |
|DEP2    |F8.0  |
+--------+------+

2.1 T-TEST.  Group Statistics
#==========#=#====#==============#========#
#     INDEP|N|Mean|Std. Deviation|SE. Mean#
#==========#=#====#==============#========#
#DEP1 1.1  |5|2.00|          .707|    .316#
#     2.1  |5|4.00|          .707|    .316#
#DEP2 1.1  |5|4.00|          .707|    .316#
#     2.1  |5|2.00|          .707|    .316#
#==========#=#====#==============#========#

2.2 T-TEST.  Independent Samples Test
#===============================#==========#===============================================================================#
#                               # Levene's |                          t-test for Equality of Means                         #
#                               #----+-----+------+-----+---------------+---------------+---------------------+------------#
#                               #    |     |      |     |               |               |                     |    95%     #
#                               #    |     |      |     |               |               |                     +------+-----#
#                               # F  | Sig.|   t  |  df |Sig. (2-tailed)|Mean Difference|Std. Error Difference| Lower|Upper#
#===============================#====#=====#======#=====#===============#===============#=====================#======#=====#
#DEP1Equal variances assumed    #.000|1.000|-4.472|    8|           .002|         -2.000|                 .447|-3.031|-.969#
#    Equal variances not assumed#    |     |-4.472|8.000|           .002|         -2.000|                 .447|-3.031|-.969#
#DEP2Equal variances assumed    #.000|1.000| 4.472|    8|           .002|          2.000|                 .447|  .969|3.031#
#    Equal variances not assumed#    |     | 4.472|8.000|           .002|          2.000|                 .447|  .969|3.031#
#===============================#====#=====#======#=====#===============#===============#=====================#======#=====#
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
