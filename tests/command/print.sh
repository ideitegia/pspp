#!/bin/sh

# This program tests unusual aspects of the PRINT and WRITE
# transformations:
#
#   - PRINT puts spaces between variables, unless a format 
#     is specified explicitly.
#
#   - WRITE doesn't put space between variables.
#
#   - PRINT to an external file prefixes each line with a space.
#
#   - PRINT EJECT to an external file indicates a formfeed by a "1"
#     in the first column.
#
#   - WRITE writes out spaces for system-missing values, not a period.
#
#   - When no output is specified, an empty record is output.

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
cat > $TEMPDIR/print.stat << foobar
data list notable /x y 1-2.
begin data.
12
34
 6 
7 
90
end data.

print /x y.
print eject /x y 1-2.
print /x '-' y.
print.

print outfile='print.out' /x y.
print eject outfile='print.out' /x y (f1,f1).
print outfile='print.out' /x '-' y.
print outfile='print.out'.

write outfile='write.out' /x y.
write outfile='write.out' /x y (2(f1)).
write outfile='write.out' /x '-' y.
write outfile='write.out'.

execute.
foobar
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode --error-file=$TEMPDIR/errs $TEMPDIR/print.stat 
if [ $? -ne 0 ] ; then fail ; fi

activity="compare print.out"
diff $TEMPDIR/print.out - <<EOF
 1 2 
112
 1 -2 
 
 3 4 
134
 3 -4 
 
 . 6 
1.6
 . -6 
 
 7 . 
17.
 7 -. 
 
 9 0 
190
 9 -0 
 
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="compare write.out"
diff $TEMPDIR/write.out - <<EOF
12
12
1-2

34
34
3-4

 6
 6
 -6

7 
7 
7- 

90
90
9-0

EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output"
diff $TEMPDIR/pspp.list - << EOF
1 2 

12
1 -2 

3 4 

34
3 -4 

. 6 

.6
. -6 

7 . 

7.
7 -. 

9 0 

90
9 -0 

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
