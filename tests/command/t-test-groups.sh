#!/bin/sh

# This program tests that the T-TEST /GROUPS command works

TEMPDIR=/tmp/pspp-tst-$$

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
cat > $TEMPDIR/out.stat <<EOF
data list list /id * indep * dep1 * dep2 *.
begin data.
1  1 1 3
2  1 2 4
3  1 2 4 
4  1 2 4 
5  1 3 5
6  2 3 1
7  2 4 2
8  2 4 2
9  2 4 2
10 2 5 3
end data.

t-test /GROUPS=indep(1,2) /var=dep1 dep2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$here/../src/pspp -o raw-ascii $TEMPDIR/out.stat
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
#DEP1 1    |5|2.00|          .707|    .316#
#     2    |5|4.00|          .707|    .316#
#DEP2 1    |5|4.00|          .707|    .316#
#     2    |5|2.00|          .707|    .316#
#==========#=#====#==============#========#

2.2 T-TEST.  Independent Samples Test
#===============================#======#===============================================================================#
#                               #Levene|                          t-test for Equality of Means                         #
#                               #-+----+------+-----+---------------+---------------+---------------------+------------#
#                               # |    |      |     |               |               |                     |    95%     #
#                               # |    |      |     |               |               |                     +------+-----#
#                               #F|Sig.|   t  |  df |Sig. (2-tailed)|Mean Difference|Std. Error Difference| Lower|Upper#
#===============================#=#====#======#=====#===============#===============#=====================#======#=====#
#DEP1Equal variances assumed    # |    |-4.472|    8|           .002|         -2.000|                 .447|-3.031|-.969#
#    Equal variances not assumed# |    |-4.472|8.000|           .002|         -2.000|                 .447|-3.031|-.969#
#DEP2Equal variances assumed    # |    | 4.472|    8|           .002|          2.000|                 .447|  .969|3.031#
#    Equal variances not assumed# |    | 4.472|8.000|           .002|          2.000|                 .447|  .969|3.031#
#===============================#=#====#======#=====#===============#===============#=====================#======#=====#
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
