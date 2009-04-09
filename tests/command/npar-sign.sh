#!/bin/sh

# This program tests the SIGN subcommand of npar tests

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

activity="create program 1"
cat > $TESTFILE <<  EOF
set format = F9.3.

data list notable list /age * height rank *.
begin data.
10 12 11
12 13 13 
13 14 12
12 12 10
9   9 10
10.3 10.2 12
end data.

npar tests
	/sign=age height WITH height rank (PAIRED)
	/MISSING ANALYSIS
	/METHOD=EXACT
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output 1"
diff - $TEMPDIR/pspp.list <<EOF
1.1 NPAR TESTS.  Frequencies
#=================================#=#
#                                 |N#
#---------------------------------+-#
#height - age Negative Differences|1#
#             Positive Differences|3#
#             Ties                |2#
#             Total               |6#
#---------------------------------+-#
#rank - heightNegative Differences|3#
#             Positive Differences|2#
#             Ties                |1#
#             Total               |6#
#=================================#=#

1.2 NPAR TESTS.  Test Statistics
#=====================#============#=============#
#                     |height - age|rank - height#
#=====================#============#=============#
#Exact Sig. (2-tailed)|        .625|        1.000#
#Exact Sig. (1-tailed)|        .312|         .500#
#Point Probability    |        .250|         .312#
#=====================#============#=============#

EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
