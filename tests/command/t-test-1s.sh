#!/bin/sh

# This program tests that the T-TEST /TESTVAL command works OK

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
data list list /ID * ABC *.
begin data.
1 3.5
2 2.0
3 2.0
4 3.5
5 3.0
6 4.0
end data.

t-test /testval=2.0 /var=abc.
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
|ABC     |F8.0  |
+--------+------+

2.1 T-TEST.  One-Sample Statistics
#===#=#====#==============#========#
#   #N|Mean|Std. Deviation|SE. Mean#
#===#=#====#==============#========#
#ABC#6|3.00|           .84|    .342#
#===#=#====#==============#========#

2.2 T-TEST.  One-Sample Test
#===#=====================================================#
#   #                Test Value = 2.000000                #
#   #-----+--+---------------+---------------+------------#
#   #     |  |               |               |    95%     #
#   #     |  |               |               +-----+------#
#   #  t  |df|Sig. (2-tailed)|Mean Difference|Lower| Upper#
#===#=====#==#===============#===============#=====#======#
#ABC#2.928| 5|           .033|          1.000|.1220|1.8780#
#===#=====#==#===============#===============#=====#======#
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
