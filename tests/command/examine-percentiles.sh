#!/bin/sh

# This program tests  the PERCENTILES subcommand of the EXAMINE command.
# In particular it tests that it behaves properly when there are only 
# a few cases

TEMPDIR=/tmp/pspp-tst-$$

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


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
cat > $TEMPDIR/out.stat <<EOF
DATA LIST LIST /x *.
BEGIN DATA.
2.00 
8.00 
5.00 
END DATA.

EXAMINE /x
	/PERCENTILES=HAVERAGE.

EXAMINE /x
	/PERCENTILES=WAVERAGE.

EXAMINE /x
	/PERCENTILES=ROUND.

EXAMINE /x
	/PERCENTILES=EMPIRICAL.

EXAMINE /x
	/PERCENTILES=AEMPIRICAL.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/out.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results"
diff $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from the command file.
+--------+------+
|Variable|Format|
#========#======#
|X       |F8.0  |
+--------+------+

2.1 EXAMINE.  Case Processing Summary
#=#=============================#
# #            Cases            #
# #---------+---------+---------#
# #  Valid  | Missing |  Total  #
# #-+-------+-+-------+-+-------#
# #N|Percent|N|Percent|N|Percent#
#=#=#=======#=#=======#=#=======#
#X#3|   100%|0|     0%|3|   100%#
#=#=#=======#=#=======#=#=======#

2.2 EXAMINE.  Percentiles
#================#================================#
#                #             Percentiles        #
#                #---+---+----+----+----+----+----#
#                # 5 | 10| 25 | 50 | 75 | 90 | 95 #
#=#==============#===#===#====#====#====#====#====#
#X|HAverage      #.40|.80|2.00|5.00|8.00|8.00|8.00#
# |Tukey's Hinges#   |   |3.50|5.00|6.50|    |    #
#=#==============#===#===#====#====#====#====#====#

3.1 EXAMINE.  Case Processing Summary
#=#=============================#
# #            Cases            #
# #---------+---------+---------#
# #  Valid  | Missing |  Total  #
# #-+-------+-+-------+-+-------#
# #N|Percent|N|Percent|N|Percent#
#=#=#=======#=#=======#=#=======#
#X#3|   100%|0|     0%|3|   100%#
#=#=#=======#=#=======#=#=======#

3.2 EXAMINE.  Percentiles
#==================#================================#
#                  #             Percentiles        #
#                  #---+---+----+----+----+----+----#
#                  # 5 | 10| 25 | 50 | 75 | 90 | 95 #
#=#================#===#===#====#====#====#====#====#
#X|Weighted Average#.30|.60|1.50|3.50|5.75|7.10|7.55#
# |Tukey's Hinges  #   |   |3.50|5.00|6.50|    |    #
#=#================#===#===#====#====#====#====#====#

4.1 EXAMINE.  Case Processing Summary
#=#=============================#
# #            Cases            #
# #---------+---------+---------#
# #  Valid  | Missing |  Total  #
# #-+-------+-+-------+-+-------#
# #N|Percent|N|Percent|N|Percent#
#=#=#=======#=#=======#=#=======#
#X#3|   100%|0|     0%|3|   100%#
#=#=#=======#=#=======#=#=======#

4.2 EXAMINE.  Percentiles
#================#================================#
#                #             Percentiles        #
#                #---+---+----+----+----+----+----#
#                # 5 | 10| 25 | 50 | 75 | 90 | 95 #
#=#==============#===#===#====#====#====#====#====#
#X|Rounded       #.00|.00|2.00|5.00|5.00|8.00|8.00#
# |Tukey's Hinges#   |   |3.50|5.00|6.50|    |    #
#=#==============#===#===#====#====#====#====#====#

5.1 EXAMINE.  Case Processing Summary
#=#=============================#
# #            Cases            #
# #---------+---------+---------#
# #  Valid  | Missing |  Total  #
# #-+-------+-+-------+-+-------#
# #N|Percent|N|Percent|N|Percent#
#=#=#=======#=#=======#=#=======#
#X#3|   100%|0|     0%|3|   100%#
#=#=#=======#=#=======#=#=======#

5.2 EXAMINE.  Percentiles
#================#==================================#
#                #              Percentiles         #
#                #----+----+----+----+----+----+----#
#                #  5 | 10 | 25 | 50 | 75 | 90 | 95 #
#=#==============#====#====#====#====#====#====#====#
#X|Empirical     #2.00|2.00|2.00|5.00|8.00|8.00|8.00#
# |Tukey's Hinges#    |    |3.50|5.00|6.50|    |    #
#=#==============#====#====#====#====#====#====#====#

6.1 EXAMINE.  Case Processing Summary
#=#=============================#
# #            Cases            #
# #---------+---------+---------#
# #  Valid  | Missing |  Total  #
# #-+-------+-+-------+-+-------#
# #N|Percent|N|Percent|N|Percent#
#=#=#=======#=#=======#=#=======#
#X#3|   100%|0|     0%|3|   100%#
#=#=#=======#=#=======#=#=======#

6.2 EXAMINE.  Percentiles
#==========================#==================================#
#                          #              Percentiles         #
#                          #----+----+----+----+----+----+----#
#                          #  5 | 10 | 25 | 50 | 75 | 90 | 95 #
#=#========================#====#====#====#====#====#====#====#
#X|Empirical with averaging#2.00|2.00|2.00|5.00|8.00|8.00|8.00#
# |Tukey's Hinges          #    |    |3.50|5.00|6.50|    |    #
#=#========================#====#====#====#====#====#====#====#

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
