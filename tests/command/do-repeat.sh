#!/bin/sh

# This program tests the DO REPEAT command.

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
cat > $TESTFILE << EOF
DATA LIST NOTABLE /a 1.
BEGIN DATA.
0
END DATA.

DO REPEAT h = h0 TO h3 / x = 0 TO 3 / y = 8, 7.5, 6, 5.
	COMPUTE h = x + y.
END REPEAT.

VECTOR v(6).
COMPUTE #idx = 0.
DO REPEAT i = 1 TO 2.
	DO REPEAT j = 3 TO 5.
		COMPUTE #x = i + j.
		COMPUTE #idx = #idx + 1.
		COMPUTE v(#idx) = #x.
	END REPEAT.
END REPEAT.

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE 
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - <<EOF
a       h0       h1       h2       h3       v1       v2       v3       v4       v5       v6
- -------- -------- -------- -------- -------- -------- -------- -------- -------- --------
0     8.00     8.50     8.00     8.00     4.00     5.00     6.00     5.00     6.00     7.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass
