#!/bin/sh

# This program tests the aggregate procedure

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/aggregate.pspp


# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp


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
	/N = n
	/NI = n./
	NU = nu
	/NUI = nu./
	NFGT2 = fgt(n, 2)
	/NFGT2I = fgt.(n, 2)
	/SFGT2 = fgt(s, '2')
	/SFGT2I = fgt.(s, '2')
	/NFIN23 = fin(n, 2, 3)
	/NFIN23I = fin.(n, 2, 3)
	/SFIN23 = fin(s, '2', '3')
	/SFIN23I = fin.(s, '2', '3')
	/NFLT2 = flt(n, 2)
	/NFLT2I = flt.(n, 2)
	/SFLT2 = flt(s, '2')
	/SFLT2I = flt.(s, '2')
	/NFIRST = first(n)
	/NFIRSTI = first.(n)
	/SFIRST = first(s)
	/SFIRSTI = first.(s)
	/NFOUT23 = fout(n, 3, 2)
	/NFOUT23I = fout.(n, 3, 2)
	/SFOUT23 = fout(s, '3', '2')
	/SFOUT23I = fout.(s, '3', '2')
	/NLAST = last(n)
	/NLASTI = last.(n)
	/SLAST = last(s)
	/SLASTI = last.(s)
	/NMAX = max(n)
	/NMAXI = max.(n)
	/SMAX = max(s)
	/SMAXI = max.(s)
	/NMEAN = mean(n)
	/NMEANI = mean.(n)
	/NMIN = min(n)
	/NMINI = min.(n)
	/SMIN = min(s)
	/SMINI = min.(s)
	/NN = n(n)
	/NNI = n.(n)
	/SN = n(s)
	/SNI = n.(s)
	/NNMISS = nmiss(n)
	/NNMISSI = nmiss.(n)
	/SNMISS = nmiss(s)
	/SNMISSI = nmiss.(s)
	/NNU = nu(n)
	/NNUI = nu.(n)
	/SNU = nu(s)
	/SNUI = nu.(s)
	/NNUMISS = numiss(n)
	/NNUMISSI = numiss.(n)
	/SNUMISS = numiss(s)
	/SNUMISSI = numiss.(s)
	/NPGT2 = pgt(n, 2)
	/NPGT2I = pgt.(n, 2)
	/SPGT2 = pgt(s, '2')
	/SPGT2I = pgt.(s, '2')
	/NPIN23 = pin(n, 2, 3)
	/NPIN23I = pin.(n, 2, 3)
	/SPIN23 = pin(s, '2', '3')
	/SPIN23I = pin.(s, '2', '3')
	/NPLT2 = plt(n, 2)
	/NPLT2I = plt.(n, 2)
	/SPLT2 = plt(s, '2')
	/SPLT2I = plt.(s, '2')
	/NPOUT23 = pout(n, 2, 3)
	/NPOUT23I = pout.(n, 2, 3)
	/SPOUT23 = pout(s, '2', '3')
	/SPOUT23I = pout.(s, '2', '3')
	/NSD = sd(n)
	/NSDI = sd.(n)
	/NSUM = sum(n)
	/NSUMI = sum.(n).
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

for outfile in scratch active external; do
    for sort in presorted unsorted; do
	for missing in itemwise columnwise; do
	    name=$outfile-$sort-$missing

	    activity="create $name.pspp"
	    {
		echo "data list notable file='aggregate.data' /G N 1-2 S 3(a) W 4."
		echo "weight by w."
		echo "missing values n(4) s('4')."
		if [ "$sort" = "presorted" ]; then
		    echo "sort cases by g."
		fi
		echo "aggregate"
		if [ "$outfile" = "active" ]; then
		    echo "	outfile=*"
		elif [ "$outfile" = "external" ]; then
		    echo "	outfile='aggregate.sys'"
		else
		    echo "	outfile=#AGGREGATE"
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
		elif [ "$outfile" = "scratch" ]; then
		    echo "get file=#AGGREGATE."
		fi
		echo "list."
	    } > $name.pspp
	    if [ $? -ne 0 ] ; then no_result ; fi
	    
	    activity="run $name.pspp"
	    $SUPERVISOR $PSPP --testing-mode -o raw-ascii -e /dev/null $name.pspp 
	    if [ $? -ne 0 ] ; then no_result ; fi

	    activity="check $name output"
	    perl -pi -e 's/^\s*$//g' pspp.list agg-$missing.out
	    diff -b -w pspp.list agg-$missing.out
	    if [ $? -ne 0 ] ; then fail ; fi
	done
    done
done

pass;
