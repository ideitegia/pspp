#!/bin/sh

# This program tests use of the VALUELABEL function in expressions.

TEMPDIR=/tmp/pspp-tst-$$

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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

activity="create program"
cat > $TEMPDIR/valuelabel.stat <<EOF
DATA LIST notable /n 1 s 2(a).
VALUE LABELS /n 0 'Very dissatisfied'
                1 'Dissatisfied'
		1.5 'Slightly Peeved'
                2 'Neutral'
                3 'Satisfied'
                4 'Very satisfied'.
VALUE LABELS /s 'a' 'Wouldn''t buy again'
                'b' 'Unhappy'
                'c' 'Bored'
                'd' 'Satiated'
                'e' 'Elated'.
STRING nlabel slabel(a10).
COMPUTE nlabel = VALUELABEL(n).
COMPUTE slabel = VALUELABEL(s).
LIST.
BEGIN DATA.

0a
1b
2c
3d
4e
5f
6g
END DATA.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/valuelabel.stat
if [ $? -ne 0 ] ; then fail ; fi

activity="compare results"
diff -c $TEMPDIR/pspp.csv - <<EOF
Table: Data List
n,s,nlabel,slabel
.,,,
0,a,Very dissa,Wouldn't b
1,b,Dissatisfi,Unhappy   
2,c,Neutral   ,Bored     
3,d,Satisfied ,Satiated  
4,e,Very satis,Elated    
5,f,,
6,g,,
EOF
if [ $? -ne 0 ] ; then fail ; fi



pass;
