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
     7.71 
     2.99 
      .21 
     4.95 
     6.34 
     4.43 
     7.49 
     8.32 
     4.99 
     5.83 
     2.25 
      .25 
     1.98 
     7.09 
     7.61 
     2.66 
     1.69 
     2.64 
      .88 
     1.50 

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
