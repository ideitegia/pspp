#!/bin/sh

# This program tests the BINOMIAL subcommand of the NPAR TESTS command.

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


# Tests for exact calculations
activity="create file 1"
cat <<EOF > $TESTFILE
ECHO 'P < 0.5; N1/N2 < 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   6
2   15
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.3) = x
	.


ECHO 'P < 0.5; N1/N2 > 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   7
2   6
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.4) = x
	.



ECHO 'P < 0.5; N1/N2 = 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   8
2   8
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.4) = x
	.

ECHO 'P > 0.5; N1/N2 < 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   11
2   12
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.6) = x
	.


ECHO 'P > 0.5; N1/N2 > 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   11
2   9
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.6) = x

ECHO 'P > 0.5; N1/N2 == 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   11
2   11
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.6) = x
	.

ECHO 'P == 0.5; N1/N2 < 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   8
2   15
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.5) = x
	.


ECHO 'P == 0.5; N1/N2 > 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   12
2   6
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.5) = x

ECHO 'P == 0.5; N1/N2 == 1' .

NEW FILE.

DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1   10
2   10
END DATA.

WEIGHT BY w.

NPAR TESTS
	/BINOMIAL(0.5) = x
	.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 1"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output 1"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff $TEMPDIR/pspp.list - << EOF
P < 0.5; N1/N2 < 1
1.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (1-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00| 6|          .286|      .300|                 .551|
| |Group2#    2.00|15|          .714|          |                     |
| |Total #        |21|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
P < 0.5; N1/N2 > 1
2.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (1-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00| 7|          .538|      .400|                 .229|
| |Group2#    2.00| 6|          .462|          |                     |
| |Total #        |13|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
P < 0.5; N1/N2 = 1
3.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (1-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00| 8|          .500|      .400|                 .284|
| |Group2#    2.00| 8|          .500|          |                     |
| |Total #        |16|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
P > 0.5; N1/N2 < 1
4.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (1-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00|11|          .478|      .600|                 .164|
| |Group2#    2.00|12|          .522|          |                     |
| |Total #        |23|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
P > 0.5; N1/N2 > 1
5.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (1-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00|11|          .550|      .600|                 .404|
| |Group2#    2.00| 9|          .450|          |                     |
| |Total #        |20|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
P > 0.5; N1/N2 == 1
6.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (1-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00|11|          .500|      .600|                 .228|
| |Group2#    2.00|11|          .500|          |                     |
| |Total #        |22|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
P == 0.5; N1/N2 < 1
7.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (2-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00| 8|          .348|      .500|                 .210|
| |Group2#    2.00|15|          .652|          |                     |
| |Total #        |23|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
P == 0.5; N1/N2 > 1
8.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (2-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00|12|          .667|      .500|                 .238|
| |Group2#    2.00| 6|          .333|          |                     |
| |Total #        |18|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
P == 0.5; N1/N2 == 1
9.1 NPAR TESTS.  Binomial Test
+-+------#--------+--+--------------+----------+---------------------+
| |      #Category| N|Observed Prop.|Test Prop.|Exact Sig. (2-tailed)|
+-+------#--------+--+--------------+----------+---------------------+
|x|Group1#    1.00|10|          .500|      .500|                1.000|
| |Group2#    2.00|10|          .500|          |                     |
| |Total #        |20|          1.00|          |                     |
+-+------#--------+--+--------------+----------+---------------------+
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
