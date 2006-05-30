#!/bin/sh

# This program tests the PRINT transformation

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

# Copy this file --- it's shared with another test
activity="create data"
cp $top_srcdir/tests/data-list.data $TEMPDIR
if [ $? -ne 0 ] ; then no_result ; fi


activity="create program"
cat > $TEMPDIR/print.stat << foobar
title 'Test PRINT transformation'.

data list free table file='$TEMPDIR/data-list.data'/A B C D.
print outfile="foo" table/A(f8.2) '/' B(e8.2) '/' C(n10) '/'.
print space a.
print outfile="foo" /a b c d.
list.

data list list table file='$TEMPDIR/data-list.data'/A B C D.
print table/A B C D.
list.

foobar
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii --testing-mode $TEMPDIR/print.stat > $TEMPDIR/errs
# Note   vv   --- there are errors in input.  Therefore, the  command must FAIL
if [ $? -eq 0 ] ; then fail ; fi

activity="compare error messages"
diff -w $TEMPDIR/errs - <<EOF
$TEMPDIR/data-list.data:1: error: (columns 1-5, field type F8.0) Field does not form a valid floating-point constant.
$TEMPDIR/data-list.data:1: warning: LIST: The expression on PRINT SPACE evaluated to the system-missing value.
$TEMPDIR/data-list.data:2: error: (columns 1-8, field type F8.0) Field does not form a valid floating-point constant.
$TEMPDIR/data-list.data:4: warning: LIST: The expression on PRINT SPACE evaluated to the system-missing value.
$TEMPDIR/data-list.data:4: error: (columns 3-12, field type F8.0) Field does not form a valid floating-point constant.
$TEMPDIR/data-list.data:6: warning: LIST: The expression on PRINT SPACE evaluated to the system-missing value.
$TEMPDIR/data-list.data:1: error: (columns 1-5, field type F8.0) Field does not form a valid floating-point constant.
$TEMPDIR/data-list.data:2: error: (columns 1-8, field type F8.0) Field does not form a valid floating-point constant.
$TEMPDIR/data-list.data:2: warning: LIST: Missing value(s) for all variables from C onward.  These will be filled with the system-missing value or blanks, as appropriate.
$TEMPDIR/data-list.data:3: warning: LIST: Missing value(s) for all variables from B onward.  These will be filled with the system-missing value or blanks, as appropriate.
$TEMPDIR/data-list.data:4: error: (columns 3-12, field type F8.0) Field does not form a valid floating-point constant.
$TEMPDIR/data-list.data:4: warning: LIST: Missing value(s) for all variables from C onward.  These will be filled with the system-missing value or blanks, as appropriate.
$TEMPDIR/data-list.data:5: warning: LIST: Missing value(s) for all variables from C onward.  These will be filled with the system-missing value or blanks, as appropriate.
$TEMPDIR/data-list.data:6: warning: LIST: Missing value(s) for all variables from B onward.  These will be filled with the system-missing value or blanks, as appropriate.
EOF
if [ $? -ne 0 ] ; then fail ; fi


activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from "$TEMPDIR/data-list.data".
+--------+------+
|Variable|Format|
#========#======#
|A       |F8.0  |
|B       |F8.0  |
|C       |F8.0  |
|D       |F8.0  |
+--------+------+
2.1 PRINT.  Writing 1 record to "foo".
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|A       |     1|  1-  8|F8.2  |
|"/"     |     1|  9-  9|A1    |
|B       |     1| 10- 17|E8.2  |
|"/"     |     1| 18- 18|A1    |
|C       |     1| 19- 28|N10.0 |
|"/"     |     1| 29- 29|A1    |
+--------+------+-------+------+
       A        B        C        D
-------- -------- -------- --------
     .       2.00     3.00     4.00 
     .       6.00     7.00     8.00 
     .      10.00    11.00    12.00 
3.1 DATA LIST.  Reading free-form data from "$TEMPDIR/data-list.data".
+--------+------+
|Variable|Format|
#========#======#
|A       |F8.0  |
|B       |F8.0  |
|C       |F8.0  |
|D       |F8.0  |
+--------+------+
4.1 PRINT.  Writing 1 record.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|A       |     1|  1-  8|F8.2  |
|B       |     1| 10- 17|F8.2  |
|C       |     1| 19- 26|F8.2  |
|D       |     1| 28- 35|F8.2  |
+--------+------+-------+------+
     .       2.00     3.00     4.00 
       A        B        C        D
-------- -------- -------- --------
     .       2.00     3.00     4.00 
     .       6.00      .        .   
     .       6.00      .        .   
    7.00      .        .        .   
    7.00      .        .        .   
    8.00      .        .        .   
    8.00      .        .        .   
   10.00    11.00      .        .   
   10.00    11.00      .        .   
   12.00      .        .        .   
   12.00      .        .        .   
EOF
if [ $? -ne 0 ] ; then fail ; fi


activity="compare print out"
diff $TEMPDIR/foo - << EOF
     .  /2.00E+00/0000000003/
     .       2.00     3.00     4.00 
     .  /6.00E+00/0000000007/
     .       6.00     7.00     8.00 
     .  /1.00E+01/0000000011/
     .      10.00    11.00    12.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
