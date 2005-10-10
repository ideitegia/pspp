#!/bin/sh

# This program tests that the T-TEST /GROUPS command works properly 
# when a single value in the independent variable is given.

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
data list list /INDEP * DEP *.
begin data.
       1        6
       1        6
       1        7
       1        6
       1       13
       1        4
       1        7
       1        9
       1        7
       1       12
       1       11
       2       11
       2        9
       2        8
       2        4
       2       16
       2        9
       2        9
       2        5
       2        4
       2       10
       2       14
end data.
t-test /groups=indep(1.514) /var=dep.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
perl -pi -e s/^\s*\$//g $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF | perl -e 's/^\s*$//g'
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|INDEP   |F8.0  |
|DEP     |F8.0  |
+--------+------+

2.1 T-TEST.  Group Statistics
#===========#==#====#==============#========#
#     INDEP | N|Mean|Std. Deviation|SE. Mean#
#===========#==#====#==============#========#
#DEP < 1.514|11|8.00|         2.864|    .863#
#    >=1.514|11|9.00|         3.821|   1.152#
#===========#==#====#==============#========#

2.2 T-TEST.  Independent Samples Test
#==============================#=========#===============================================================================#
#                              #Levene's |                          t-test for Equality of Means                         #
#                              #----+----+-----+------+---------------+---------------+---------------------+------------#
#                              #    |    |     |      |               |               |                     |    95%     #
#                              #    |    |     |      |               |               |                     +------+-----#
#                              #  F |Sig.|  t  |  df  |Sig. (2-tailed)|Mean Difference|Std. Error Difference| Lower|Upper#
#==============================#====#====#=====#======#===============#===============#=====================#======#=====#
#DEPEqual variances assumed    #.172|.683|-.695|    20|           .495|         -1.000|                1.440|-4.003|2.003#
#   Equal variances not assumed#    |    |-.695|18.539|           .496|         -1.000|                1.440|-4.018|2.018#
#==============================#====#====#=====#======#===============#===============#=====================#======#=====#
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
