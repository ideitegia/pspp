#!/bin/sh

# This program tests MISSING VALUES

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


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

# Copy this file --- it's shared with another test
activity="create data"
cp $top_srcdir/tests/data-list.data $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi


activity="create program"
cat > $TEMPDIR/missing-values.stat << foobar
DATA LIST NOTABLE/str1 1-5 (A) str2 6-8 (A) date1 9-19 (DATE) num1 20-25.

/* Valid: numeric missing values.
MISSING VALUES date1 num1 (1).
MISSING VALUES date1 num1 (1, 2).
MISSING VALUES date1 num1 (1, 2, 3).

/* Valid: numeric missing values using the first variable's format.
MISSING VALUES num1 date1 ('1').
MISSING VALUES num1 date1 ('1', '2').
MISSING VALUES num1 date1 ('1', '2', '3').
MISSING VALUES date1 num1 ('06-AUG-05').
MISSING VALUES date1 num1 ('06-AUG-05', '01-OCT-78').
MISSING VALUES date1 num1 ('06-AUG-05', '01-OCT-78', '14-FEB-81').

/* Valid: ranges of numeric missing values.
MISSING VALUES num1 (1 THRU 2).
MISSING VALUES num1 (LO THRU 2).
MISSING VALUES num1 (LOWEST THRU 2).
MISSING VALUES num1 (1 THRU HI).
MISSING VALUES num1 (1 THRU HIGHEST).

/* Valid: a range of numeric missing values, plus an individual value.
MISSING VALUES num1 (1 THRU 2, 3).
MISSING VALUES num1 (LO THRU 2, 3).
MISSING VALUES num1 (LOWEST THRU 2, 3).
MISSING VALUES num1 (1 THRU HI, -1).
MISSING VALUES num1 (1 THRU HIGHEST, -1).

/* Valid: string missing values.
MISSING VALUES str1 str2 ('abc  ','def').

/* Invalid: too long for str2.
MISSING VALUES str1 str2 ('abcde').

/* Invalid: no string ranges.
MISSING VALUES str1 ('a' THRU 'z').

/* Invalid: mixing string and numeric variables.
MISSING VALUES str1 num1 ('123').

/* Valid: may mix variable types when clearing missing values.
MISSING VALUES ALL ().

foobar
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii --testing-mode $TEMPDIR/missing-values.stat > $TEMPDIR/errs
# Note   vv   --- there are errors in input.  Therefore, the  command must FAIL
if [ $? -eq 0 ] ; then fail ; fi

activity="compare error messages"
diff -w $TEMPDIR/errs - <<EOF
$TEMPDIR/missing-values.stat:34: error: MISSING VALUES: Missing values provided are too long to assign to variable of width 3.
$TEMPDIR/missing-values.stat:34: warning: Skipping the rest of this command.  Part of this command may have been executed.
$TEMPDIR/missing-values.stat:37: error: MISSING VALUES: Syntax error expecting string at \`THRU'.
$TEMPDIR/missing-values.stat:37: error: MISSING VALUES: THRU is not a variable name.
$TEMPDIR/missing-values.stat:37: warning: Skipping the rest of this command.  Part of this command may have been executed.
$TEMPDIR/missing-values.stat:40: error: MISSING VALUES: Cannot mix numeric variables (e.g. num1) and string variables (e.g. str1) within a single list.
$TEMPDIR/missing-values.stat:40: warning: Skipping the rest of this command.  Part of this command may have been executed.
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
