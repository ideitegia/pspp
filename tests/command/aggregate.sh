#!/bin/sh

# This program tests the aggregate procedure

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/aggregate.pspp


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

activity="data create"
cat > aggregate.data <<EOF
2 42
1001
4 41
3112
1112
2661
1221
2771
1331
1441
2881
1551
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="command skeleton create"
cat > agg-skel.pspp <<EOF
	/document
	/break=g
	/n = n
	/ni = n./
	nu = nu
	/nui = nu./
	nfgt2 = fgt(n, 2)
	/nfgt2i = fgt.(n, 2)
	/sfgt2 = fgt(s, '2')
	/sfgt2i = fgt.(s, '2')
	/nfin23 = fin(n, 2, 3)
	/nfin23i = fin.(n, 2, 3)
	/sfin23 = fin(s, '2', '3')
	/sfin23i = fin.(s, '2', '3')
	/nflt2 = flt(n, 2)
	/nflt2i = flt.(n, 2)
	/sflt2 = flt(s, '2')
	/sflt2i = flt.(s, '2')
	/nfirst = first(n)
	/nfirsti = first.(n)
	/sfirst = first(s)
	/sfirsti = first.(s)
	/nfout23 = fout(n, 3, 2)
	/nfout23i = fout.(n, 3, 2)
	/sfout23 = fout(s, '3', '2')
	/sfout23i = fout.(s, '3', '2')
	/nlast = last(n)
	/nlasti = last.(n)
	/slast = last(s)
	/slasti = last.(s)
	/nmax = max(n)
	/nmaxi = max.(n)
	/smax = max(s)
	/smaxi = max.(s)
	/nmean = mean(n)
	/nmeani = mean.(n)
	/nmin = min(n)
	/nmini = min.(n)
	/smin = min(s)
	/smini = min.(s)
	/nn = n(n)
	/nni = n.(n)
	/sn = n(s)
	/sni = n.(s)
	/nnmiss = nmiss(n)
	/nnmissi = nmiss.(n)
	/snmiss = nmiss(s)
	/snmissi = nmiss.(s)
	/nnu = nu(n)
	/nnui = nu.(n)
	/snu = nu(s)
	/snui = nu.(s)
	/nnumiss = numiss(n)
	/nnumissi = numiss.(n)
	/snumiss = numiss(s)
	/snumissi = numiss.(s)
	/npgt2 = pgt(n, 2)
	/npgt2i = pgt.(n, 2)
	/spgt2 = pgt(s, '2')
	/spgt2i = pgt.(s, '2')
	/npin23 = pin(n, 2, 3)
	/npin23i = pin.(n, 2, 3)
	/spin23 = pin(s, '2', '3')
	/spin23i = pin.(s, '2', '3')
	/nplt2 = plt(n, 2)
	/nplt2i = plt.(n, 2)
	/splt2 = plt(s, '2')
	/splt2i = plt.(s, '2')
	/npout23 = pout(n, 2, 3)
	/npout23i = pout.(n, 2, 3)
	/spout23 = pout(s, '2', '3')
	/spout23i = pout.(s, '2', '3')
	/nsd = sd(n)
	/nsdi = sd.(n)
	/nsum = sum(n)
	/nsumi = sum.(n).
EOF

activity="expected output (itemwise missing) create"
cat > agg-itemwise.out <<EOF
G        N       NI      NU     NUI NFGT2 NFGT2I SFGT2 SFGT2I NFIN23 NFIN23I SFIN23 SFIN23I NFLT2 NFLT2I SFLT2 SFLT2I NFIRST NFIRSTI SFIRST SFIRSTI NFOUT23 NFOUT23I SFOUT23 SFOUT23I NLAST NLASTI SLAST SLASTI NMAX NMAXI SMAX SMAXI    NMEAN   NMEANI NMIN NMINI SMIN SMINI       NN      NNI       SN      SNI   NNMISS  NNMISSI   SNMISS  SNMISSI     NNU    NNUI     SNU    SNUI NNUMISS NNUMISSI SNUMISS SNUMISSI NPGT2 NPGT2I SPGT2 SPGT2I NPIN23 NPIN23I SPIN23 SPIN23I NPLT2 NPLT2I SPLT2 SPLT2I NPOUT23 NPOUT23I SPOUT23 SPOUT23I      NSD     NSDI     NSUM    NSUMI
- -------- -------- ------- ------- ----- ------ ----- ------ ------ ------- ------ ------- ----- ------ ----- ------ ------ ------- ------ ------- ------- -------- ------- -------- ----- ------ ----- ------ ---- ----- ---- ----- -------- -------- ---- ----- ---- ----- -------- -------- -------- -------- -------- -------- -------- -------- ------- ------- ------- ------- ------- -------- ------- -------- ----- ------ ----- ------ ------ ------- ------ ------- ----- ------ ----- ------ ------- -------- ------- -------- -------- -------- -------- --------
1     7.00     7.00       6       6  .333   .429  .333   .429   .333    .286   .333    .286  .500   .429  .500   .429      0       0      0       0    .667     .714    .667     .714     5      5     5      5    5     5    5     5     2.00     2.29    0     0    0     0     6.00     7.00     6.00     7.00     1.00      .00     1.00      .00       5       6       5       6       1        0       1        0  33.3   42.9  33.3   42.9   33.3    28.6   33.3    28.6  50.0   42.9  50.0   42.9    66.7     71.4    66.7     71.4     1.79     1.80    12.00    16.00 
2     5.00     5.00       4       4 1.000  1.000 1.000  1.000   .000    .000   .000    .000  .000   .000  .000   .000      6       6      6       4   1.000    1.000   1.000    1.000     8      8     8      8    8     8    8     8     7.00     7.00    6     6    6     4     3.00     3.00     3.00     5.00     2.00     2.00     2.00      .00       3       3       3       4       1        1       1        0 100.0  100.0 100.0  100.0     .0      .0     .0      .0    .0     .0    .0     .0   100.0    100.0   100.0    100.0     1.00     1.00    21.00    21.00 
3     2.00     2.00       1       1  .000   .000  .000   .000   .000    .000   .000    .000 1.000  1.000 1.000  1.000      1       1      1       1   1.000    1.000   1.000    1.000     1      1     1      1    1     1    1     1     1.00     1.00    1     1    1     1     2.00     2.00     2.00     2.00      .00      .00      .00      .00       1       1       1       1       0        0       0        0    .0     .0    .0     .0     .0      .0     .0      .0 100.0  100.0 100.0  100.0   100.0    100.0   100.0    100.0      .00      .00     2.00     2.00 
4     1.00     1.00       1       1  .      .     .     1.000   .       .      .       .000  .      .     .      .000      .       .              4    .        .       .       1.000     .      .            4    .     .          4      .        .      .     .          4      .00      .00      .00     1.00     1.00     1.00     1.00      .00       0       0       0       1       1        1       1        0    .      .     .   100.0     .       .      .       .0    .      .     .      .0      .        .       .     100.0      .        .        .        .   
EOF

activity="expected output (columnwise missing) create"
cat > agg-columnwise.out <<EOF
G        N       NI      NU     NUI NFGT2 NFGT2I SFGT2 SFGT2I NFIN23 NFIN23I SFIN23 SFIN23I NFLT2 NFLT2I SFLT2 SFLT2I NFIRST NFIRSTI SFIRST SFIRSTI NFOUT23 NFOUT23I SFOUT23 SFOUT23I NLAST NLASTI SLAST SLASTI NMAX NMAXI SMAX SMAXI    NMEAN   NMEANI NMIN NMINI SMIN SMINI       NN      NNI       SN      SNI   NNMISS  NNMISSI   SNMISS  SNMISSI     NNU    NNUI     SNU    SNUI NNUMISS NNUMISSI SNUMISS SNUMISSI NPGT2 NPGT2I SPGT2 SPGT2I NPIN23 NPIN23I SPIN23 SPIN23I NPLT2 NPLT2I SPLT2 SPLT2I NPOUT23 NPOUT23I SPOUT23 SPOUT23I      NSD     NSDI     NSUM    NSUMI
- -------- -------- ------- ------- ----- ------ ----- ------ ------ ------- ------ ------- ----- ------ ----- ------ ------ ------- ------ ------- ------- -------- ------- -------- ----- ------ ----- ------ ---- ----- ---- ----- -------- -------- ---- ----- ---- ----- -------- -------- -------- -------- -------- -------- -------- -------- ------- ------- ------- ------- ------- -------- ------- -------- ----- ------ ----- ------ ------ ------- ------ ------- ----- ------ ----- ------ ------- -------- ------- -------- -------- -------- -------- --------
1     7.00     7.00       6       6  .      .429  .      .429   .       .286   .       .286  .      .429  .      .429      .       0              0    .        .714    .        .714     .      5            5    .     5          5      .       2.29    .     0          0     6.00     7.00     6.00     7.00     1.00      .00     1.00      .00       5       6       5       6       1        0       1        0    .    42.9    .    42.9     .     28.6     .     28.6    .    42.9    .    42.9      .      71.4      .      71.4      .       1.80      .      16.00 
2     5.00     5.00       4       4  .      .     .     1.000   .       .      .       .000  .      .     .      .000      .       .              4    .        .       .       1.000     .      .            8    .     .          8      .        .      .     .          4     3.00     3.00     3.00     5.00     2.00     2.00     2.00      .00       3       3       3       4       1        1       1        0    .      .     .   100.0     .       .      .       .0    .      .     .      .0      .        .       .     100.0      .        .        .        .   
3     2.00     2.00       1       1  .000   .000  .000   .000   .000    .000   .000    .000 1.000  1.000 1.000  1.000      1       1      1       1   1.000    1.000   1.000    1.000     1      1     1      1    1     1    1     1     1.00     1.00    1     1    1     1     2.00     2.00     2.00     2.00      .00      .00      .00      .00       1       1       1       1       0        0       0        0    .0     .0    .0     .0     .0      .0     .0      .0 100.0  100.0 100.0  100.0   100.0    100.0   100.0    100.0      .00      .00     2.00     2.00 
4     1.00     1.00       1       1  .      .     .     1.000   .       .      .       .000  .      .     .      .000      .       .              4    .        .       .       1.000     .      .            4    .     .          4      .        .      .     .          4      .00      .00      .00     1.00     1.00     1.00     1.00      .00       0       0       0       1       1        1       1        0    .      .     .   100.0     .       .      .       .0    .      .     .      .0      .        .       .     100.0      .        .        .        .   
EOF

for outfile in active external; do
    for sort in presorted unsorted; do
	for missing in itemwise columnwise; do
	    name=$outfile-$sort-$missing

	    activity="create $name.pspp"
	    {
		echo "data list notable file='aggregate.data' /g n 1-2 s 3(a) w 4."
		echo "weight by w."
		echo "missing values n(4) s('4')."
		if [ "$sort" = "presorted" ]; then
		    echo "sort cases by g."
		fi
		echo "aggregate"
		if [ "$outfile" = "active" ]; then
		    echo "	outfile=*"
		else
		    echo "	outfile='aggregate.sys'"
		fi
		if [ "$sort" = "presorted" ]; then
		    echo "	/presorted"
		fi
		if [ "$missing" = "columnwise" ]; then
		    echo "	/missing=columnwise"
		fi
		cat agg-skel.pspp
		if [ "$outfile" = "external" ]; then
		    echo "get file='aggregate.sys'."
		fi
		echo "list."
	    } > $name.pspp
	    if [ $? -ne 0 ] ; then no_result ; fi
	    
	    activity="run $name.pspp"
	    $SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $name.pspp >/dev/null 2>&1
	    if [ $? -ne 0 ] ; then no_result ; fi

	    activity="check $name output"
	    diff -b -w -B pspp.list agg-$missing.out
	    if [ $? -ne 0 ] ; then fail ; fi
	done
    done
done

exit 0
pass;
