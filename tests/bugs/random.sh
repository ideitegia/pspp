#!/bin/sh

# This program tests for a bug which caused UNIFORM(x) to always return zero.


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
cat > $TEMPDIR/rnd.sps <<EOF
set seed=10.
input program.
+ loop #i = 1 to 20.
+    do repeat response=r1.
+       compute response = uniform(10).
+    end repeat.
+    end case.
+ end loop.
+ end file.
end input program.                                                              

list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/rnd.sps
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output"
diff -b -B -w $TEMPDIR/pspp.list - << EOF
      R1
--------
    2.36 
    3.13 
    1.76 
     .15 
    5.88 
    8.74 
    2.19 
    6.53 
    5.69 
    6.77 
    7.20 
    4.01 
     .03 
    4.67 
    5.10 
     .44 
    8.27 
    6.81 
    9.55 
    8.74 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
