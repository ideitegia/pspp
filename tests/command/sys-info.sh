#!/bin/sh

# This program tests the SYSFILE INFO command

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

activity="create program 1"
cat > $TEMPDIR/save.stat << EOF
data list /x 1-2.
begin data.
3
34
2
98
end data.
save 'foo.save'.
display $JDATE.
finish.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 1"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TEMPDIR/save.stat
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 2"
cat > $TEMPDIR/read.stat << EOF
sysfile info file='foo.save'.

finish.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 2"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TEMPDIR/read.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
diff -B -b $TEMPDIR/pspp.list - << EOF
1.1 SYSFILE INFO.  
File:      $TEMPDIR/foo.save
Label:     No label.
Created:   18 Dec 03 09:05:20 by GNU pspp 0.3.1 - i686-pc-cygwin        
Endian:    Little.
Variables: 1
Cases:     4
Type:      System File.
Weight:    Not weighted.
Mode:      Compression on.
+--------+-------------+---+
|Variable|Description  |Pos|
|        |             |iti|
|        |             |on |
#========#=============#===#
|X       |Format: F2.0 |  1|
+--------+-------------+---+
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
