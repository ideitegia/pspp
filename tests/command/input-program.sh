#!/bin/sh

# This program tests the INPUT PROGRAM command, specifically all of
# the examples given in the user manual.

TEMPDIR=/tmp/pspp-tst-$$

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

activity="create a.data"
cat > a.data <<EOF
1
2
3
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create b.data"
cat > b.data <<EOF
4
5
6
7
8
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create test1.pspp"
cat > test1.pspp <<EOF
INPUT PROGRAM.
        DATA LIST NOTABLE FILE='a.data'/X 1-10.
        DATA LIST NOTABLE FILE='b.data'/Y 1-10.
END INPUT PROGRAM.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run test1"
$SUPERVISOR $PSPP --testing-mode test1.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare test1 results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
         X          Y
---------- ----------
         1          4
         2          5
         3          6
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="create test2.pspp"
cat > test2.pspp <<EOF
INPUT PROGRAM.
        NUMERIC #A #B.

        DO IF NOT #A.
                DATA LIST NOTABLE END=#A FILE='a.data'/X 1-10.
        END IF.
        DO IF NOT #B.
                DATA LIST NOTABLE END=#B FILE='b.data'/Y 1-10.
        END IF.
        DO IF #A AND #B.
                END FILE.
        END IF.
        END CASE.
END INPUT PROGRAM.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run test2"
$SUPERVISOR $PSPP --testing-mode test2.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare test2 results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
         X          Y
---------- ----------
         1          4
         2          5
         3          6
         .          7
         .          8
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="create test3.pspp"
cat > test3.pspp <<EOF
INPUT PROGRAM.
        NUMERIC #A #B.

        DO IF #A.
                DATA LIST NOTABLE END=#B FILE='b.data'/X 1-10.
                DO IF #B.
                        END FILE.
                ELSE.
                        END CASE.
                END IF.
        ELSE.
                DATA LIST NOTABLE END=#A FILE='a.data'/X 1-10.
                DO IF NOT #A.
                        END CASE.
                END IF.
        END IF.
END INPUT PROGRAM.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run test3"
$SUPERVISOR $PSPP --testing-mode test3.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare test3 results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
         X
----------
         1
         2
         3
         4
         5
         6
         7
         8
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="create test4.pspp"
cat > test4.pspp <<EOF
INPUT PROGRAM.
        NUMERIC #EOF.

        LOOP IF NOT #EOF.
                DATA LIST NOTABLE END=#EOF FILE='a.data'/X 1-10.
                DO IF NOT #EOF.
                        END CASE.
                END IF.
        END LOOP.

        COMPUTE #EOF = 0.
        LOOP IF NOT #EOF.
                DATA LIST NOTABLE END=#EOF FILE='b.data'/X 1-10.
                DO IF NOT #EOF.
                        END CASE.
                END IF.
        END LOOP.

        END FILE.
END INPUT PROGRAM.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run test4"
$SUPERVISOR $PSPP --testing-mode test4.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare test4 results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
         X
----------
         1
         2
         3
         4
         5
         6
         7
         8
EOF
if [ $? -ne 0 ] ; then fail ; fi

# This example differs slightly from the one in the manual in that
# it doesn't generate random variates.  There's already a test that
# checks that random variates are predictable, so we don't need
# another.
activity="create test5.pspp"
cat > test5.pspp <<EOF
INPUT PROGRAM.
        LOOP #I=1 TO 50.
                COMPUTE X=#I * 3.
                END CASE.
        END LOOP.
        END FILE.
END INPUT PROGRAM.
LIST/FORMAT=NUMBERED.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run test5"
$SUPERVISOR $PSPP --testing-mode test5.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare test5 results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << EOF
Case#        X
----- --------
    1     3.00
    2     6.00
    3     9.00
    4    12.00
    5    15.00
    6    18.00
    7    21.00
    8    24.00
    9    27.00
   10    30.00
   11    33.00
   12    36.00
   13    39.00
   14    42.00
   15    45.00
   16    48.00
   17    51.00
   18    54.00
   19    57.00
   20    60.00
   21    63.00
   22    66.00
   23    69.00
   24    72.00
   25    75.00
   26    78.00
   27    81.00
   28    84.00
   29    87.00
   30    90.00
   31    93.00
   32    96.00
   33    99.00
   34   102.00
   35   105.00
   36   108.00
   37   111.00
   38   114.00
   39   117.00
   40   120.00
   41   123.00
   42   126.00
   43   129.00
   44   132.00
   45   135.00
   46   138.00
   47   141.00
   48   144.00
   49   147.00
   50   150.00
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
