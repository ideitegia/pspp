#!/bin/sh

# This program tests for a bug in the paired samples T test.
# Thanks to Mike Griffiths for reporting this problem.

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

cat >> $TESTFILE <<EOF
set format f8.3.
data list list /A * B *.
begin data.
11 2
1  1
1  1
end data.

t-test pairs = a with b (paired).
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b  $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from INLINE.
+--------+------+
|Variable|Format|
#========#======#
|A       |F8.0  |
|B       |F8.0  |
+--------+------+
2.1 T-TEST.  Paired Sample Statistics
#========#=====#=#==============#========#
#        # Mean|N|Std. Deviation|SE. Mean#
#========#=====#=#==============#========#
#Pair 0 A#4.333|3|         5.774|   3.333#
#       B#1.333|3|          .577|    .333#
#========#=====#=#==============#========#
2.2 T-TEST.  Paired Samples Correlations
#======#=====#=#===========#====#
#      |     #N|Correlation|Sig.#
#======#=====#=#===========#====#
#Pair 0|A & B#3|      1.000|.000#
#======#=====#=#===========#====#
2.3 T-TEST.  Paired Samples Test
#===========#==================================================#=====#==#===============#
#           #                Paired Differences                |     |  |               #
#           #-----+--------------+---------------+-------------+     |  |               #
#           #     |              |               |     95%     |     |  |               #
#           #     |              |               +------+------+     |  |               #
#           # Mean|Std. Deviation|Std. Error Mean| Lower| Upper|  t  |df|Sig. (2-tailed)#
#===========#=====#==============#===============#======#======#=====#==#===============#
#Pair 0A - B#3.000|         5.196|          3.000|-9.908|15.908|1.000| 2|           .423#
#===========#=====#==============#===============#======#======#=====#==#===============#
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
