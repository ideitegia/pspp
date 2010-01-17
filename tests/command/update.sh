#!/bin/sh

# This program tests the UPDATE procedure

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/update.pspp


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

activity="data create"
cat > a.data <<EOF
1aB
8aM
3aE
5aG
0aA
5aH
6aI
7aJ
2aD
7aK
1aC
7aL
4aF
EOF
if [ $? -ne 0 ] ; then no_result ; fi
cat > b.data <<EOF
1bN
3bO
4bP
6bQ
7bR
9bS
EOF
if [ $? -ne 0 ] ; then no_result ; fi

cat > update.csv <<EOF
Table: Data List
A,B,C,D,INA,INB
0,a,A,,1,0
1,b,B,N,1,1
1,a,C,,1,0
2,a,D,,1,0
3,b,E,O,1,1
4,b,F,P,1,1
5,a,G,,1,0
5,a,H,,1,0
6,b,I,Q,1,1
7,b,J,R,1,1
7,a,K,,1,0
7,a,L,,1,0
8,a,M,,1,0
9,b,,S,0,1
EOF

# Test UPDATE.
dla="data list notable file='a.data' /A B C 1-3 (a)."
sa="save outfile='a.sys'."
dlb="data list notable file='b.data' /A B C 1-3 (a)."
sb="save outfile='b.sys'."
for sources in ss sa as; do
    name="$sources"
    activity="create $name.pspp"
    {
	if [ $sources = ss ]; then
	    cat <<EOF
set errors=terminal.
$dla
$sa
$dlb
$sb
update file='a.sys' /in=INA /sort
         /file='b.sys' /in=INB /rename c=D
         /by a.
EOF
	elif [ $sources = sa ]; then
	    cat <<EOF
set errors=terminal.
$dla
$sa
$dlb

update file='a.sys' /in=INA /sort
      /file=* /in=INB /rename c=D
      /by a.
EOF
	elif [ $sources = as ]; then
	    cat <<EOF
set errors=terminal.
$dlb
$sb
$dla

update file=* /in=INA /sort
      /file='b.sys' /in=INB /rename c=D
      /by a.
EOF
	else
	    activity="internal error"
	    no_result
	fi
	echo 'list.'
    } > $name.pspp
    if [ $? -ne 0 ] ; then no_result ; fi

    activity="run $name.pspp"
    rm -f errors
    $SUPERVISOR $PSPP --testing-mode --error-file=errors $name.pspp
    if [ $? -ne 0 ] ; then no_result ; fi

    activity="check $name output"
    perl -pi -e 's/^\s*$//g' pspp.csv
    diff -c pspp.csv update.csv
    if [ $? -ne 0 ] ; then fail ; fi
    diff -c -b -w - errors <<EOF
$name.pspp:8: warning: UPDATE: Encountered 3 sets of duplicate cases in the master file.
EOF
    if [ $? -ne 0 ] ; then fail ; fi
done

pass;
