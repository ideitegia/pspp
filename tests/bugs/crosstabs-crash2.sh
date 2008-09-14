#!/bin/sh

# This program tests for bug #22037, which caused CROSSTABS to crash.

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

activity="create program"
cat > $TESTFILE <<EOF
data list list /x * y (a18).

begin data.

   1. 'zero none'

1 'one unity'
2 'two duality'
3 'three lots'
end data.

CROSSTABS /TABLES = x BY y.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

$SUPERVISOR $PSPP --testing-mode $TESTFILE > /dev/null
if [ $? -ne 0 ] ; then no_result ; fi

perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  -w $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from INLINE.
+--------+------+
|Variable|Format|
#========#======#
|x       |F8.0  |
|y       |A18   |
+--------+------+
$TEMPDIR/crosstabs-crash2.sh.sps:4: warning: BEGIN DATA: Missing value(s) for all variables from x onward.  These will be filled with the system-missing value or blanks, as appropriate.
$TEMPDIR/crosstabs-crash2.sh.sps:6: warning: BEGIN DATA: Missing value(s) for all variables from x onward.  These will be filled with the system-missing value or blanks, as appropriate.
2.1 CROSSTABS.  Summary.
#===============#=====================================================#
#               #                        Cases                        #
#               #-----------------+-----------------+-----------------#
#               #      Valid      |     Missing     |      Total      #
#               #--------+--------+--------+--------+--------+--------#
#               #       N| Percent|       N| Percent|       N| Percent#
#---------------#--------+--------+--------+--------+--------+--------#
#x * y          #       4|   66.7%|       2|   33.3%|       6|  100.0%#
#===============#========#========#========#========#========#========#
2.2 CROSSTABS.  x by y [count].
#===============#===================================#========#
#               #                 y                 |        #
#               #--------+--------+--------+--------+        #
#              x#one unit|three lo|two dual|zero non|  Total #
#---------------#--------+--------+--------+--------+--------#
#           1.00#     1.0|      .0|      .0|     1.0|     2.0#
#           2.00#      .0|      .0|     1.0|      .0|     1.0#
#           3.00#      .0|     1.0|      .0|      .0|     1.0#
#Total          #     1.0|     1.0|     1.0|     1.0|     4.0#
#===============#========#========#========#========#========#
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
