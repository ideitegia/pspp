#! /bin/sh

# Tests calculation of moments.

TEMPDIR=/tmp/pspp-tst-$$

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
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
activity="create one-pass moments list"
sed -ne 's/#.*//;/^[ 	]*$/!p' > $TEMPDIR/moments-list-1p <<'EOF'
# Both the one-pass and two-pass algorithms should be 
# able to cope properly with these.
1 2 3 4 => W=4.000 M1=2.500 M2=1.667 M3=0.000 M4=-1.200
1*5 2*5 3*5 4*5 => W=20.000 M1=2.500 M2=1.316 M3=0.000 M4=-1.401
1*1 2*2 3*3 4*4 => W=10.000 M1=3.000 M2=1.111 M3=-0.712 M4=-0.450
1*0 => W=0.000 M1=sysmis M2=sysmis M3=sysmis M4=sysmis
1*1 => W=1.000 M1=1.000 M2=sysmis M3=sysmis M4=sysmis
1*2 => W=2.000 M1=1.000 M2=0.000 M3=sysmis M4=sysmis
1*3 => W=3.000 M1=1.000 M2=0.000 M3=sysmis M4=sysmis
1*2 3 => W=3.000 M1=1.667 M2=1.333 M3=1.732 M4=sysmis
1 1.00000001 => W=2.000 M1=1.000 M2=0.000 M3=sysmis M4=sysmis
1000001 1000002 1000003 1000004 => W=4.000 M1=1000002.500 M2=1.667 M3=0.000 M4=-1.200
EOF
if [ $? -ne 0 ] ; then no_result ; fi

cp $TEMPDIR/moments-list-1p $TEMPDIR/moments-list-2p
sed -ne 's/#.*//;/^[ 	]*$/!p' >> $TEMPDIR/moments-list-2p <<'EOF'
# We used to have an example for which only the two-pass algorithm
# produced reasonable results, but the provisional means algorithm
# does better, so there aren't any extra tests here.
EOF

activity="create two-pass input file"
sed < $TEMPDIR/moments-list-2p >> $TEMPDIR/moments-2p.stat \
	-e 's#^\(.*\) => \(.*\)$#DEBUG MOMENTS/\1.#'
if [ $? -ne 0 ] ; then no_result ; fi

activity="run two-pass program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii \
	 $TEMPDIR/moments-2p.stat >$TEMPDIR/moments-2p.err 2> $TEMPDIR/moments-2p.out

activity="compare two-pass output"
diff -B -b $TEMPDIR/moments-list-2p $TEMPDIR/moments-2p.out
if [ $? -ne 0 ] ; then fail ; fi

activity="create input file"
sed < $TEMPDIR/moments-list-1p >> $TEMPDIR/moments-1p.stat \
	-e 's#^\(.*\) => \(.*\)$#DEBUG MOMENTS ONEPASS/\1.#'
if [ $? -ne 0 ] ; then no_result ; fi

activity="run one-pass program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii \
	 $TEMPDIR/moments-1p.stat >$TEMPDIR/moments-1p.err 2> $TEMPDIR/moments-1p.out

activity="compare one-pass output"
diff -B -b $TEMPDIR/moments-list-1p $TEMPDIR/moments-1p.out
if [ $? -ne 0 ] ; then fail ; fi

pass
