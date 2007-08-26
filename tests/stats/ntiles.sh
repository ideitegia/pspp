#! /bin/sh

# Tests the NTILE subcommand of the frequencies command

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


cleanup()
{
     cd /
     rm -rf $TEMPDIR
     :
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


i=1;

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /x * .
BEGIN DATA.
1 
2 
3 
4 
5
END DATA.

FREQUENCIES 
	VAR=x
	/PERCENTILES = 0 25 33.333 50 66.666 75 100

EOF
if [ $? -ne 0 ] ; then no_result; fi

activity="run program $i"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="move output"
cp $TEMPDIR/pspp.list $TEMPDIR/list.ref
if [ $? -ne 0 ] ; then no_result ; fi

i=$[$i+1];

activity="create program $i"
cat > $TEMPDIR/prog.sps <<EOF
DATA LIST LIST notable /x * .
BEGIN DATA.
1 
2 
3 
4 
5
END DATA.

FREQUENCIES 
	VAR=x
	/NTILES = 3
	/NTILES = 4
	.
EOF
if [ $? -ne 0 ] ; then no_result; fi

activity="run program $i"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/prog.sps
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff $TEMPDIR/pspp.list $TEMPDIR/list.ref
if [ $? -ne 0 ] ; then fail; fi


pass;
