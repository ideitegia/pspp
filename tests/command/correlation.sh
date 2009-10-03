#!/bin/sh

# This program tests the CORRELATIONS command

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
cat << EOF > $TESTFILE
set format = F11.3.
data list notable list /foo * bar * wiz * bang *.
begin data.
1   0   3   1
3   9 -50   5
3   4   3 203
4  -9   0  -4
98 78 104   2
3  50 -49 200
.   4   4   4
5   3   0   .
end data.

correlations 
	variables = foo bar wiz bang
	/print nosig
	/missing = listwise
	.

correlations 
	variables = bar wiz
	/print nosig
	/missing = listwise
	.

correlations 
	variables = foo bar wiz bang
	/print nosig
	/missing = pairwise
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
cp $TEMPDIR/pspp.list /tmp
diff -b  $TEMPDIR/pspp.list - << EOF
1.1 CORRELATIONS.  Correlations
#========================#=====#=====#=====#=====#
#                        #foo  |bar  |wiz  |bang #
#----+-------------------#-----+-----+-----+-----#
#foo |Pearson Correlation#1.000| .802| .890|-.308#
#    |Sig. (2-tailed)    #     | .055| .017| .553#
#----+-------------------#-----+-----+-----+-----#
#bar |Pearson Correlation# .802|1.000| .519| .118#
#    |Sig. (2-tailed)    # .055|     | .291| .824#
#----+-------------------#-----+-----+-----+-----#
#wiz |Pearson Correlation# .890| .519|1.000|-.344#
#    |Sig. (2-tailed)    # .017| .291|     | .505#
#----+-------------------#-----+-----+-----+-----#
#bang|Pearson Correlation#-.308| .118|-.344|1.000#
#    |Sig. (2-tailed)    # .553| .824| .505|     #
#====#===================#=====#=====#=====#=====#
2.1 CORRELATIONS.  Correlations
#=======================#=====#=====#
#                       #bar  |wiz  #
#---+-------------------#-----+-----#
#bar|Pearson Correlation#1.000| .497#
#   |Sig. (2-tailed)    #     | .210#
#---+-------------------#-----+-----#
#wiz|Pearson Correlation# .497|1.000#
#   |Sig. (2-tailed)    # .210|     #
#===#===================#=====#=====#
3.1 CORRELATIONS.  Correlations
#========================#=====#=====#=====#=====#
#                        #foo  |bar  |wiz  |bang #
#----+-------------------#-----+-----+-----+-----#
#foo |Pearson Correlation#1.000| .805| .883|-.308#
#    |Sig. (2-tailed)    #     | .029| .008| .553#
#    |N                  #    7|    7|    7|    6#
#----+-------------------#-----+-----+-----+-----#
#bar |Pearson Correlation# .805|1.000| .497| .164#
#    |Sig. (2-tailed)    # .029|     | .210| .725#
#    |N                  #    7|    8|    8|    7#
#----+-------------------#-----+-----+-----+-----#
#wiz |Pearson Correlation# .883| .497|1.000|-.337#
#    |Sig. (2-tailed)    # .008| .210|     | .460#
#    |N                  #    7|    8|    8|    7#
#----+-------------------#-----+-----+-----+-----#
#bang|Pearson Correlation#-.308| .164|-.337|1.000#
#    |Sig. (2-tailed)    # .553| .725| .460|     #
#    |N                  #    6|    7|    7|    7#
#====#===================#=====#=====#=====#=====#
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
