#!/bin/sh

# This program tests the RANK command.

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


# Some tests for proper behaviour in the face of invalid input.
activity="create file 1"
cat > $TESTFILE <<EOF
DATA LIST LIST NOTABLE /x * a (a2).
BEGIN DATA.
-1 s
0  s
1  s
2  s
2  s
4  s
5  s
END DATA.

DEBUG XFORM FAIL.

RANK x.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

# Check that it properly handles failed transformations.
activity="run program 1"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii -e $TEMPDIR/err $TESTFILE 
if [ $? -ne 1 ] ; then fail ; fi

activity="diff 1"
perl -pi -e 's/^\s*$//g' $TEMPDIR/err
diff  -b $TEMPDIR/err - <<EOF
$TEMPDIR/rank.sh.sps:14: error: Stopping syntax file processing here to avoid a cascade of dependent command failures.
EOF
if [ $? -ne 0 ] ; then fail ; fi


#Now for some general error conditions.
activity="create file 2"
cat > $TESTFILE <<EOF
DATA LIST LIST NOTABLE /x * a (a2).
BEGIN DATA.
-1 s
0  s
1  s
2  s
2  s
4  s
5  s
END DATA.

* invalid NTILES (no parameter)
RANK x 
  /NTILES
.

* invalid NTILES (not an integer)
RANK x 
  /NTILES(d)
.


* destination variable already exists
RANK x 
 /RANK INTO x.


* Too many variables in INTO
RANK x 
 /RANK INTO foo  bar wiz.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program (syntax errors)"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii -e $TEMPDIR/errs $TESTFILE 
if [ $? -ne 1 ] ; then fail ; fi

activity="compare errors"
perl -pi -e 's/^\s*$//g' $TEMPDIR/errs
diff  -b $TEMPDIR/errs - << EOF
$TEMPDIR/rank.sh.sps:15: error: RANK: Syntax error expecting \`(' at end of command.
$TEMPDIR/rank.sh.sps:19: error: RANK: Syntax error expecting integer at \`d'.
$TEMPDIR/rank.sh.sps:25: error: RANK: Variable x already exists.
$TEMPDIR/rank.sh.sps:30: error: RANK: Too many variables in INTO clause.
EOF
if [ $? -ne 0 ] ; then fail ; fi

# Now some real tests.

activity="create file 3"
cat > $TESTFILE <<EOF
DATA LIST LIST NOTABLE /x (f8).
BEGIN DATA.
-1
0 
1 
2 
2 
4 
5 
END DATA.

ECHO  'Simple example using defaults'.

RANK x.

LIST.

NEW FILE.
DATA LIST LIST NOTABLE /a * b *.
BEGIN DATA.
0 24
1 32
2 31
2 32
4 30
5 29
6 1
7 43
8 .
9 45
END DATA.

RANK a b (D)
   /PRINT=YES 
   /RANK
   /TIES=HIGH
   /RFRACTION
   /N INTO count
   .

DISPLAY DICTIONARY.

LIST.


ECHO  'Test variable name fallback'.

NEW FILE.
DATA LIST LIST NOTABLE /foo * rfoo * ran003 *.
BEGIN DATA.
0 3 2
1 3 2
2 3 2
2 3 2
4 3 2
5 3 2
6 3 2
7 3 2
8 3 2
9 3 2
END DATA.

RANK foo.


DISPLAY DICTIONARY.


NEW FILE.
DATA LIST LIST NOTABLE /a * b *.
BEGIN DATA.
0 24
1 32
2 31
2 32
4 30
5 29
6 1
7 43
8 8 
9 45
END DATA.

RANK a
  /PRINT=YES
  /TIES=CONDENSE
  /SAVAGE
  /PERCENT
  /PROPORTION
  /NTILES(4)
  /NORMAL
.

LIST.

NEW FILE.
DATA LIST LIST NOTABLE /a * g1 g2 *.
BEGIN DATA.
1 0 2
2 0 2
3 0 2
4 0 2
5 0 2
6 0 2
7 0 2
8 0 2
2 1 2
2 1 2
3 1 2
4 1 2
5 1 2
6 1 2
7 1 2
7 1 2
8 1 2
9 1 1
END DATA.

RANK a (D) BY g2 g1
  /PRINT=YES
  /TIES=LOW
  /MISSING=INCLUDE
  /FRACTION=RANKIT
  /RANK
  /NORMAL
  .

LIST.


ECHO 'fractional ranks ( including small ones for special case of SAVAGE ranks)'
NEW FILE.
DATA LIST LIST NOTABLE  /a *  w * .
BEGIN DATA.
1 1.5
2 0.2
3 0.1
4 1
5 1
6 1
7 1
8 1
END DATA.

WEIGHT BY w.

RANK a 
  /FRACTION=TUKEY
  /PROPORTION
  /SAVAGE
.

LIST.


ECHO 'test all the ties cases with low caseweight values'.

NEW FILE.
DATA LIST LIST NOTABLE /x * w *.
BEGIN DATA.
1 0.1
2 0.1
3 0.1
4 0.2
5 0.1
6 0.1
7 0.1
8 0.1
END DATA.

WEIGHT BY w.

RANK x
 /TIES=low
 /RANK into xl.


RANK x
 /TIES=high
 /RANK into xh.

RANK x
 /TIES=condense
 /RANK into xc.


* Test VW fraction

RANK x
 /FRACTION=VW
 /NORMAL.

LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program 3"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 3"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - <<EOF
Simple example using defaults
Variables Created By RANK
x into Rx(RANK of x)
       x        Rx
-------- ---------
      -1     1.000 
       0     2.000 
       1     3.000 
       2     4.500 
       2     4.500 
       4     6.000 
       5     7.000 
Variables Created By RANK
a into Ra(RANK of a)
b into Rb(RANK of b)
a into RFR001(RFRACTION of a)
b into RFR002(RFRACTION of b)
a into count(N of a)
b into Nb(N of b)
1.1 DISPLAY.  
+--------+-------------------------------------------+--------+
|Variable|Description                                |Position|
#========#===========================================#========#
|a       |Format: F8.2                               |       1|
+--------+-------------------------------------------+--------+
|b       |Format: F8.2                               |       2|
+--------+-------------------------------------------+--------+
|count   |N of a                                     |       3|
|        |Format: F6.0                               |        |
+--------+-------------------------------------------+--------+
|Ra      |RANK of a                                  |       4|
|        |Format: F9.3                               |        |
+--------+-------------------------------------------+--------+
|Rb      |RANK of b                                  |       5|
|        |Format: F9.3                               |        |
+--------+-------------------------------------------+--------+
|RFR001  |RFRACTION of a                             |       6|
|        |Format: F6.4                               |        |
+--------+-------------------------------------------+--------+
|RFR002  |RFRACTION of b                             |       7|
|        |Format: F6.4                               |        |
+--------+-------------------------------------------+--------+
|Nb      |N of b                                     |       8|
|        |Format: F6.0                               |        |
+--------+-------------------------------------------+--------+
       a        b  count        Ra        Rb RFR001 RFR002     Nb
-------- -------- ------ --------- --------- ------ ------ ------
     .00    24.00     10    10.000     8.000 1.0000  .8889      9 
    1.00    32.00     10     9.000     4.000  .9000  .4444      9 
    2.00    31.00     10     8.000     5.000  .8000  .5556      9 
    2.00    32.00     10     8.000     4.000  .8000  .4444      9 
    4.00    30.00     10     6.000     6.000  .6000  .6667      9 
    5.00    29.00     10     5.000     7.000  .5000  .7778      9 
    6.00     1.00     10     4.000     9.000  .4000 1.0000      9 
    7.00    43.00     10     3.000     2.000  .3000  .2222      9 
    8.00      .       10     2.000      .     .2000  .          . 
    9.00    45.00     10     1.000     1.000  .1000  .1111      9 
Test variable name fallback
Variables Created By RANK
foo into RAN001(RANK of foo)
2.1 DISPLAY.  
+--------+-------------------------------------------+--------+
|Variable|Description                                |Position|
#========#===========================================#========#
|foo     |Format: F8.2                               |       1|
+--------+-------------------------------------------+--------+
|rfoo    |Format: F8.2                               |       2|
+--------+-------------------------------------------+--------+
|ran003  |Format: F8.2                               |       3|
+--------+-------------------------------------------+--------+
|RAN001  |RANK of foo                                |       4|
|        |Format: F9.3                               |        |
+--------+-------------------------------------------+--------+
Variables Created By RANK
a into Sa(SAVAGE of a)
a into Pa(PERCENT of a)
a into PRO001(PROPORTION of a using BLOM)
a into Na(NTILES of a)
a into NOR001(NORMAL of a using BLOM)
       a        b       Sa     Pa PRO001  Na NOR001
-------- -------- -------- ------ ------ --- ------
     .00    24.00   -.9000  10.00  .0610   1 -1.547 
    1.00    32.00   -.7889  20.00  .1585   1 -1.000 
    2.00    31.00   -.5925  30.00  .2561   2 -.6554 
    2.00    32.00   -.5925  30.00  .2561   2 -.6554 
    4.00    30.00   -.3544  40.00  .3537   2 -.3755 
    5.00    29.00   -.1544  50.00  .4512   2 -.1226 
    6.00     1.00    .0956  60.00  .5488   3  .1226 
    7.00    43.00    .4290  70.00  .6463   3  .3755 
    8.00     8.00    .9290  80.00  .7439   3  .6554 
    9.00    45.00   1.9290  90.00  .8415   4 1.0005 
Variables Created By RANK
a into Ra(RANK of a BY g2 g1)
a into Na(NORMAL of a using RANKIT BY g2 g1)
       a       g1       g2        Ra     Na
-------- -------- -------- --------- ------
    1.00      .00     2.00     8.000 1.5341 
    2.00      .00     2.00     7.000  .8871 
    3.00      .00     2.00     6.000  .4888 
    4.00      .00     2.00     5.000  .1573 
    5.00      .00     2.00     4.000 -.1573 
    6.00      .00     2.00     3.000 -.4888 
    7.00      .00     2.00     2.000 -.8871 
    8.00      .00     2.00     1.000 -1.534 
    2.00     1.00     2.00     8.000  .9674 
    2.00     1.00     2.00     8.000  .9674 
    3.00     1.00     2.00     7.000  .5895 
    4.00     1.00     2.00     6.000  .2822 
    5.00     1.00     2.00     5.000  .0000 
    6.00     1.00     2.00     4.000 -.2822 
    7.00     1.00     2.00     2.000 -.9674 
    7.00     1.00     2.00     2.000 -.9674 
    8.00     1.00     2.00     1.000 -1.593 
    9.00     1.00     1.00     1.000  .0000 
fractional ranks ( including small ones for special case of SAVAGE ranks)
Variables Created By RANK
a into Pa(PROPORTION of a using TUKEY)
a into Sa(SAVAGE of a)
       a        w     Pa       Sa
-------- -------- ------ --------
    1.00     1.50  .1285   -.8016 
    2.00      .20  .1776   -.6905 
    3.00      .10  .1986   -.6905 
    4.00     1.00  .3458   -.5305 
    5.00     1.00  .4860   -.2905 
    6.00     1.00  .6262    .0262 
    7.00     1.00  .7664    .4929 
    8.00     1.00  .9065   1.3929 
test all the ties cases with low caseweight values
Variables Created By RANK
x into xl(RANK of x)
Variables Created By RANK
x into xh(RANK of x)
Variables Created By RANK
x into xc(RANK of x)
Variables Created By RANK
x into Nx(NORMAL of x using VW)
       x        w        xl        xh        xc     Nx
-------- -------- --------- --------- --------- ------
    1.00      .10      .000      .100     1.000 -1.938 
    2.00      .10      .100      .200     2.000 -1.412 
    3.00      .10      .200      .300     3.000 -1.119 
    4.00      .20      .300      .500     4.000 -.8046 
    5.00      .10      .500      .600     5.000 -.5549 
    6.00      .10      .600      .700     6.000 -.4067 
    7.00      .10      .700      .800     7.000 -.2670 
    8.00      .10      .800      .900     8.000 -.1323 
EOF
if [ $? -ne 0 ] ; then fail ; fi



# A test to make sure that variable name creation is really robust
activity="create file 4"
cat > $TESTFILE <<EOF
DATA LIST LIST notable /x * rx * ran001 TO ran999.
BEGIN DATA.
1
2
3
4
5
6
7
END DATA.

RANK x.

DELETE VAR ran001 TO ran999.

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 4"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii -e /dev/null $TESTFILE 
if [ $? -ne 0 ] ; then fail ; fi


activity="compare output 4"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff  -b $TEMPDIR/pspp.list - << EOF
Variables Created By RANK
x into RNKRA01(RANK of x)
       x       rx   RNKRA01
-------- -------- ---------
    1.00      .       1.000 
    2.00      .       2.000 
    3.00      .       3.000 
    4.00      .       4.000 
    5.00      .       5.000 
    6.00      .       6.000 
    7.00      .       7.000 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
