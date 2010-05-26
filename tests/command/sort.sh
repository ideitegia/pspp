#!/bin/sh

# This program tests the sort command

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps
: ${PERL:=perl}

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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

activity="write perl program for generating data"
cat > gen-data.pl <<'EOF'
use strict;
use warnings;

# Generate shuffled data.
my (@data);
for my $i (0...$ARGV[0] - 1) {
    push (@data, $i) foreach 1...$ARGV[1];
}
fisher_yates_shuffle (\@data);

# Output shuffled data.
my (@shuffled) = map ([$data[$_], $_], 0...$#data);
open (SHUFFLED, ">sort.in");
print SHUFFLED "$data[$_] $_\n" foreach 0...$#data;

# Output sorted data.
my (@sorted) = sort { $a->[0] <=> $b->[0] || $a->[1] <=> $b->[1] } @shuffled;
open (SORTED, ">sort.exp");
print SORTED "$_->[0] $_->[1]\n" foreach @sorted;

# From perlfaq4.
sub fisher_yates_shuffle {
    my $deck = shift;  # $deck is a reference to an array
    my $i = @$deck;
    while ($i--) {
	my $j = int rand ($i+1);
	@$deck[$i,$j] = @$deck[$j,$i];
    }
}
EOF

for count_repeat_buffers in \
    "100 5 2" "100 5 3" "100 5 4" "100 5 5" "100 5 10" "100 5 50" "100 5 100" "100 5" \
    "100 10 2" "100 10 3" "100 10 5" "100 10" \
    "1000 5 5" "1000 5 50" "1000 5" \
    "100 100 3" "100 100 5" "100 100" \
    "10000 5 500" \
    "50000 1"; do
  set $count_repeat_buffers
  count=$1
  repeat=$2
  buffers=$3

  printf .

  activity="generate data for $count_repeat_buffers run"
  $PERL gen-data.pl $count $repeat > sort.data
  if [ $? -ne 0 ] ; then no_result ; fi
  
  activity="generate test program for $count_repeat_buffers run"
  {
      echo "data list list file='sort.in'/x y (f8)."
      if test "$buffers" != ""; then
	  echo "sort by x/buffers=$buffers."
      else
	  echo "sort by x."
      fi
      echo "print outfile='sort.out'/x y."
      echo "execute."
  } > sort.pspp || no_result
  
  activity="run program"
  $SUPERVISOR $PSPP --testing-mode -o pspp.csv sort.pspp
  if [ $? -ne 0 ] ; then no_result ; fi
  
  perl -pi -e 's/^\s*$//g' sort.exp sort.out
  diff -w sort.exp sort.out
  if [ $? -ne 0 ] ; then fail ; fi
done
echo
pass;
