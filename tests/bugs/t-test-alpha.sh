#!/bin/sh

# This program tests that the T-TEST works when the independent
# variable is alpha
# BUG #11227

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
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
data list list /ID * INDEP (a1) DEP1 * DEP2 *.
begin data.
1  'a' 1 3
2  'a' 2 4
3  'a' 2 4 
4  'a' 2 4 
5  'a' 3 5
6  'b' 3 1
7  'b' 4 2
8  'b' 4 2
9  'b' 4 2
10 'b' 5 3
11 'c' 2 2
end data.


t-test /GROUPS=indep('a','b') /var=dep1 dep2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|ID      |F8.0  |
|INDEP   |A1    |
|DEP1    |F8.0  |
|DEP2    |F8.0  |
+--------+------+
2.1 T-TEST.  Group Statistics
#==========#=#====#==============#========#
#     INDEP|N|Mean|Std. Deviation|SE. Mean#
#==========#=#====#==============#========#
#DEP1 a    |5|2.00|          .707|    .316#
#     b    |5|4.00|          .707|    .316#
#DEP2 a    |5|4.00|          .707|    .316#
#     b    |5|2.00|          .707|    .316#
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
