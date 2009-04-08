#!/bin/sh

# This program tests for a bug which didn't properly
# compare string values.

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

cat >> $TESTFILE <<EOF
data list list /x * gv (a8).
begin data.
3   One
2   One
3   One
2   One
3   One
4   Two
3.5 Two
3.0 Two
end data.

t-test group=gv('One', 'Two')
	/variables = x.
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
|x       |F8.0  |
|gv      |A8    |
+--------+------+
2.1 T-TEST.  Group Statistics
#==========#=#====#==============#========#
#     gv   |N|Mean|Std. Deviation|SE. Mean#
#==========#=#====#==============#========#
#x One     |5|2.60|           .55|     .24#
#  Two     |3|3.50|           .50|     .29#
#==========#=#====#==============#========#
2.2 T-TEST.  Independent Samples Test
#============================#=========#============================================================================#
#                            # Levene's|                        t-test for Equality of Means                        #
#                            #----+----+-----+----+---------------+---------------+---------------------+-----------#
#                            #    |    |     |    |               |               |                     |    95%    #
#                            #    |    |     |    |               |               |                     +-----+-----#
#                            #  F |Sig.|  t  | df |Sig. (2-tailed)|Mean Difference|Std. Error Difference|Lower|Upper#
#============================#====#====#=====#====#===============#===============#=====================#=====#=====#
#xEqual variances assumed    #1.13| .33|-2.32|6.00|            .06|           -.90|                  .38|-1.83|  .03#
# Equal variances not assumed#    |    |-2.38|4.70|            .07|           -.90|                  .38|-1.89|  .09#
#============================#====#====#=====#====#===============#===============#=====================#=====#=====#
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
