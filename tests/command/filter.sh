#!/bin/sh

# This program tests ....

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
cat > $TEMPDIR/filter.stat << EOF
data list notable /x 1-2.
begin data.
1
2
3
4
5
6
7
8
9
10
end data.
compute filter_$ = mod(x,2).

filter by filter_$.
list.
filter off.
list.
compute filter_$ = 1 - filter_$.
filter by filter_$.
list.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$here/../src/pspp --testing-mode -o raw-ascii $TEMPDIR/filter.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="check results"
diff -B -b $TEMPDIR/pspp.list - << EOF
 X FILTER_$
-- --------
 1     1.00 
 3     1.00 
 5     1.00 
 7     1.00 
 9     1.00 

 X FILTER_$
-- --------
 1     1.00 
 2      .00 
 3     1.00 
 4      .00 
 5     1.00 
 6      .00 
 7     1.00 
 8      .00 
 9     1.00 
10      .00 

 X FILTER_$
-- --------
 2     1.00 
 4     1.00 
 6     1.00 
 8     1.00 
10     1.00 

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
