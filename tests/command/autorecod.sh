#!/bin/sh

# This program tests the autorecode command

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
cat > $TEMPDIR/prog.sps <<EOF
/* Tries AUTORECODE on some random but similar strings of characters.
data list /x 1-5(a) y 7.
begin data.
lasdj 1 1                                                           3
asdfk 0 3 <---- These are the numbers that should be produced for a 4
asdfj 2 4                                                           2
asdfj 1 4                                                           3
asdfk 2 3                                                           2
asdfj 9 4                                                           1
lajks 9 2                                                           1
asdfk 0 3 These are the numbers that should be produced for b ----> 4
asdfk 1 3                                                           3
end data.

autorecode x y into a b/descend.

list.
/* Just to make sure it works on second & subsequent executions,
/* try it again.
compute z=trunc(y/2).
autorecode z into w.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$here/../src/pspp    -o raw-ascii $TEMPDIR/prog.sps  
if [ $? -ne 0 ] ; then no_result ; fi

activity="test output"
diff -b -B $TEMPDIR/pspp.list - <<EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|X       |     1|  1-  5|A5    |
|Y       |     1|  7-  7|F1.0  |
+--------+------+-------+------+

    X Y        A        B
----- - -------- --------
lasdj 1     1.00     3.00 
asdfk 0     3.00     4.00 
asdfj 2     4.00     2.00 
asdfj 1     4.00     3.00 
asdfk 2     3.00     2.00 
asdfj 9     4.00     1.00 
lajks 9     2.00     1.00 
asdfk 0     3.00     4.00 
asdfk 1     3.00     3.00 

    X Y        A        B        Z        W
----- - -------- -------- -------- --------
lasdj 1     1.00     3.00      .00     1.00 
asdfk 0     3.00     4.00      .00     1.00 
asdfj 2     4.00     2.00     1.00     2.00 
asdfj 1     4.00     3.00      .00     1.00 
asdfk 2     3.00     2.00     1.00     2.00 
asdfj 9     4.00     1.00     4.00     3.00 
lajks 9     2.00     1.00     4.00     3.00 
asdfk 0     3.00     4.00      .00     1.00 
asdfk 1     3.00     3.00      .00     1.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass



