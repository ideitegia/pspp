#!/bin/sh

# This program tests the DATA LIST input program.

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

# Create command file.
activity="create program"
cat > $TEMPDIR/data-list.stat << EOF
data list free/A B C D.
begin data.
,1,2,3
,4,,5
6
7,
8 9
0,1,,,
,,,,
2

3
4
5
end data.
list.

data list free (tab)/A B C D.
begin data.
1	2	3	4
1	2	3	
1	2		4
1	2		
1		3	4
1		3	
1			4
1			
	2	3	4
	2	3	
	2		4
	2		
		3	4
		3	
			4
			
end data.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii --testing-mode $TEMPDIR/data-list.stat # > $TEMPDIR/errs
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output"
diff -b -B $TEMPDIR/pspp.list - << EOF
       A        B        C        D
-------- -------- -------- --------
     .       1.00     2.00     3.00 
     .       4.00      .       5.00 
    6.00     7.00     8.00     9.00 
     .00     1.00      .        .   
     .        .        .        .   
    2.00     3.00     4.00     5.00 

       A        B        C        D
-------- -------- -------- --------
    1.00     2.00     3.00     4.00 
    1.00     2.00     3.00      .   
    1.00     2.00      .       4.00 
    1.00     2.00      .        .   
    1.00      .       3.00     4.00 
    1.00      .       3.00      .   
    1.00      .        .       4.00 
    1.00      .        .        .   
     .       2.00     3.00     4.00 
     .       2.00     3.00      .   
     .       2.00      .       4.00 
     .       2.00      .        .   
     .        .       3.00     4.00 
     .        .       3.00      .   
     .        .        .       4.00 
     .        .        .        .   
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
