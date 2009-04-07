#!/bin/sh

# This program tests that the REGRESSION command works.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
    if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo Not cleaning $TEMPDIR;
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

activity="create program"
cat > $TESTFILE <<EOF
data list list / v0 to v2.
begin data
 0.65377128  7.735648 -23.97588
-0.13087553  6.142625 -19.63854
 0.34880368  7.651430 -25.26557
 0.69249021  6.125125 -16.57090
-0.07368178  8.245789 -25.80001
-0.34404919  6.031540 -17.56743
 0.75981559  9.832291 -28.35977
-0.46958313  5.343832 -16.79548
-0.06108490  8.838262 -29.25689
 0.56154863  6.200189 -18.58219
end data
regression /variables=v0 v1 v2 /statistics defaults /dependent=v2 /method=enter /save=pred resid.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from INLINE.
+--------+------+
|Variable|Format|
#========#======#
|v0      |F8.0  |
|v1      |F8.0  |
|v2      |F8.0  |
+--------+------+
2.1 REGRESSION.  Model Summary
#============#========#=================#==========================#
#          R #R Square|Adjusted R Square|Std. Error of the Estimate#
#========#===#========#=================#==========================#
#        |.97#     .94|              .93|                      1.34#
#========#===#========#=================#==========================#
2.2 REGRESSION.  ANOVA
#===================#==============#==#===========#=====#============#
#                   #Sum of Squares|df|Mean Square|  F  |Significance#
#========#==========#==============#==#===========#=====#============#
#        |Regression#        202.75| 2|     101.38|56.75|         .00#
#        |Residual  #         12.50| 7|       1.79|     |            #
#        |Total     #        215.26| 9|           |     |            #
#========#==========#==============#==#===========#=====#============#
2.3 REGRESSION.  Coefficients
#===================#=====#==========#=====#======#============#
#                   #  B  |Std. Error| Beta|   t  |Significance#
#========#==========#=====#==========#=====#======#============#
#        |(Constant)# 2.19|      2.36|  .00|   .93|         .52#
#        |    v0    # 1.81|      1.05|  .17|  1.72|         .12#
#        |    v1    #-3.43|       .33|-1.03|-10.33|         .00#
#        |          #     |          |     |      |            #
#========#==========#=====#==========#=====#======#============#
      v0       v1       v2     RES1    PRED1
-------- -------- -------- -------- --------
     .65     7.74   -23.98     -.84   -23.13 
    -.13     6.14   -19.64     -.54   -19.10 
     .35     7.65   -25.27    -1.87   -23.40 
     .69     6.13   -16.57      .97   -17.54 
    -.07     8.25   -25.80      .40   -26.20 
    -.34     6.03   -17.57     1.53   -19.10 
     .76     9.83   -28.36     1.77   -30.13 
    -.47     5.34   -16.80      .18   -16.97 
    -.06     8.84   -29.26    -1.05   -28.21 
     .56     6.20   -18.58     -.54   -18.04 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
