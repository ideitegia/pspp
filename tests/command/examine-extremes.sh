#!/bin/sh

# This program tests  the EXTREME subcommand of the EXAMINE command.

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
data list free /V1 W
begin data.
1  1
2  1
3  2
3  1
4  1
5  1
6  1
7  1
8  1
9  1
10 1
11 1
12 1
13 1
14 1
15 1
16 1
17 1
18 2
19 1
20 1
end data.

weight by w.

examine v1 
 /statistics=extreme(6)
 .
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
1.1 EXAMINE.  Case Processing Summary
#==#===============================#
#  #             Cases             #
#  #----------+---------+----------#
#  #   Valid  | Missing |   Total  #
#  #--+-------+-+-------+--+-------#
#  # N|Percent|N|Percent| N|Percent#
#==#==#=======#=#=======#==#=======#
#V1#23|   100%|0|     0%|23|   100%#
#==#==#=======#=#=======#==#=======#
1.2 EXAMINE.  Extreme Values
#============#===========#=====#
#            #Case Number|Value#
#============#===========#=====#
#V1 Highest 1#         21|20.00#
#           2#         20|19.00#
#           3#         19|18.00#
#           4#         19|18.00#
#           5#         18|17.00#
#           6#         17|16.00#
#  ----------#-----------+-----#
#    Lowest 1#          1| 1.00#
#           2#          2| 2.00#
#           3#          4| 3.00#
#           4#          3| 3.00#
#           5#          3| 3.00#
#           6#          5| 4.00#
#============#===========#=====#
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
