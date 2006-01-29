#!/bin/sh

# This program tests the DATA LIST input program.

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

# Create command file.
activity="create program"
cat > $TESTFILE << EOF
data list list ('|','X') /A B C D.
begin data.
1|23X45|2.03
2X22|34|23|
3|34|34X34
end data.

list.

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
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
1.1 DATA LIST.  Reading free-form data from INLINE.
+--------+------+
|Variable|Format|
#========#======#
|A       |F8.0  |
|B       |F8.0  |
|C       |F8.0  |
|D       |F8.0  |
+--------+------+
       A        B        C        D
-------- -------- -------- --------
    1.00    23.00    45.00     2.03 
    2.00    22.00    34.00    23.00 
    3.00    34.00    34.00    34.00 
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
