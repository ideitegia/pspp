#!/bin/sh

# This program tests the MATCH FILES procedure

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/match-files.pspp


here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`


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

activity="data create"
cat > a.data <<EOF
0aA
1aB
1aC
2aD
3aE
4aF
5aG
5aH
6aI
7aJ
7aK
7aL
8aM
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

cat > ff.out <<EOF
A B C D INA INB
- - - - --- ---
0 a A     1   0
1 a B N   1   1
1 a C     1   0
2 a D     1   0
3 a E O   1   1
4 a F P   1   1
5 a G     1   0
5 a H     1   0
6 a I Q   1   1
7 a J R   1   1
7 a K     1   0
7 a L     1   0
8 a M     1   0
9 b   S   0   1
EOF

cat > ft.out <<EOF
A B C D INA INB
- - - - --- ---
0 a A     1   0
1 a B N   1   1
1 a C N   1   1
2 a D     1   0
3 a E O   1   1
4 a F P   1   1
5 a G     1   0
5 a H     1   0
6 a I Q   1   1
7 a J R   1   1
7 a K R   1   1
7 a L R   1   1
8 a M     1   0
EOF

# Test nonparallel match and table lookup.
dla="data list notable file='a.data' /A B C 1-3 (a)."
sa="save outfile='a.sys'."
dlb="data list notable file='b.data' /A B C 1-3 (a)."
sb="save outfile='b.sys'."
for types in ff ft; do
    type1=file
    if [ $types = ff ]; then 
	type2=file
    else
	type2=table
    fi
    for sources in ss sa as; do
	name="$types-$sources"
	activity="create $name.pspp"
	{
	    if [ $sources = ss ]; then
		cat <<EOF
$dla
$sa
$dlb
$sb
match files $type1='a.sys' /in=INA /$type2='b.sys' /in=INB /rename c=D /by a.
EOF
	    elif [ $sources = sa ]; then
		cat <<EOF
$dla
$sa
$dlb
match files $type1='a.sys' /in=INA /$type2=* /in=INB /rename c=D /by a.
EOF
	    elif [ $sources = as ]; then
		cat <<EOF
$dlb
$sb
$dla
match files $type1=* /in=INA /$type2='b.sys' /in=INB /rename c=D /by a.
EOF
	    else
		activity="internal error"
		no_result
	    fi
	    echo 'list.'
        } > $name.pspp
	if [ $? -ne 0 ] ; then no_result ; fi

	activity="run $name.pspp"
	$SUPERVISOR $here/../src/pspp -o raw-ascii $name.pspp >/dev/null 2>&1
	if [ $? -ne 0 ] ; then no_result ; fi

	activity="check $name output"
	perl -pi -e 's/^\s*$//g' pspp.list
	perl -pi -e 's/^\s*$//g' $types.out
	diff -b -w pspp.list $types.out
	if [ $? -ne 0 ] ; then fail ; fi
    done
done

# Test parallel match.	
name="parallel"
activity="create $name.pspp"
cat > $name.pspp <<EOF
$dla
$sa
$dlb
$sb
match files file='a.sys' /file='b.sys' /rename (a b c=D E F).
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run $name.pspp"
$SUPERVISOR $here/../src/pspp -o raw-ascii $name.pspp >/dev/null 2>&1
if [ $? -ne 0 ] ; then no_result ; fi

activity="check $name output"
perl -pi -e 's/^\s*$//g' pspp.list
diff -b -w - pspp.list <<EOF
A B C D E F
- - - - - -
0 a A 1 b N
1 a B 3 b O
1 a C 4 b P
2 a D 6 b Q
3 a E 7 b R
4 a F 9 b S
5 a G
5 a H
6 a I
7 a J
7 a K
7 a L
8 a M
EOF
if [ $? -ne 0 ] ; then fail ; fi

# Test bug handling TABLE from active file found by John Darrington.
name="active-table"
activity="create $name.pspp"
cat > $name.pspp <<EOF
DATA LIST LIST NOTABLE /x * y *.
BEGIN DATA
3 30
2 21
1 22
END DATA.

SAVE OUTFILE='bar.sav'.

DATA LIST LIST NOTABLE /x * z *.
BEGIN DATA
3 8
2 9
END DATA.

MATCH FILES TABLE=* /FILE='bar.sav' /BY=x.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run $name.pspp"
$SUPERVISOR $here/../src/pspp -o raw-ascii $name.pspp >/dev/null 2>&1
if [ $? -ne 0 ] ; then no_result ; fi

activity="check $name output"
perl -pi -e 's/^\s*$//g' pspp.list
diff -b -w - pspp.list <<EOF | perl -e 's/^\s*$//g'
        x        z        y
 -------- -------- --------
     3.00     8.00    30.00 
     2.00      .      21.00 
     1.00      .      22.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
