#!/bin/sh

# This program tests the LOOP command

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
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR" 
	return ; 
     fi
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

activity="create prog"
cat > $TEMPDIR/loop.stat <<EOF
data list notable /x 1 y 2 z 3.
begin data.
121
252
393
404
end data.

echo 'Loop with index'.
loop #i=x to y by z.
print /#i.
end loop.
print/'--------'.
execute.

echo 'Loop with IF condition'.
compute #j=x.
loop if #j <= y.
print /#j.
compute #j = #j + z.
end loop.
print/'--------'.
execute.

echo 'Loop with END IF condition'.
compute #k=x.
loop.
print /#k.
compute #k = #k + z.
end loop if #k > y.
print/'--------'.
execute.

echo 'Loop with index and IF condition based on index'.
loop #m=x to y by z if #m < 4.
print /#m.
end loop.
print/'--------'.
execute.

echo 'Loop with index and END IF condition based on index'.
loop #n=x to y by z.
print /#n.
end loop if #n >= 4.
print/'--------'.
execute.

echo 'Loop with index and IF and END IF condition based on index'.
loop #o=x to y by z if mod(#o,2) = 0.
print /#o.
end loop if #o >= 4.
print/'--------'.
execute.

echo 'Loop with no conditions'.
set mxloops = 2.
compute #p = x.
loop.
print /#p.
compute #p = #p + z.
do if #p >= y.
break.
end if.
end loop.
print/'--------'.
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv -e $TEMPDIR/stdout $TEMPDIR/loop.stat 
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare stdout"
perl -pi -e 's/^\s*$//g' $TEMPDIR/stdout
diff -b $TEMPDIR/stdout  - <<EOF
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="compare results"
diff -c $TEMPDIR/pspp.csv  - <<EOF
Loop with index

1.00 

2.00 

--------

2.00 

4.00 

--------

3.00 

6.00 

9.00 

--------

--------

Loop with IF condition

1.00 

2.00 

--------

2.00 

4.00 

--------

3.00 

6.00 

9.00 

--------

--------

Loop with END IF condition

1.00 

2.00 

--------

2.00 

4.00 

--------

3.00 

6.00 

9.00 

--------

4.00 

--------

Loop with index and IF condition based on index

1.00 

2.00 

--------

2.00 

--------

3.00 

--------

--------

Loop with index and END IF condition based on index

1.00 

2.00 

--------

2.00 

4.00 

--------

3.00 

6.00 

--------

--------

Loop with index and IF and END IF condition based on index

--------

2.00 

4.00 

--------

--------

--------

Loop with no conditions

1.00 

--------

2.00 

4.00 

--------

3.00 

6.00 

--------

4.00 

--------
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
