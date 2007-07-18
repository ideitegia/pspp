#! /usr/bin/perl -w

use strict;
use warnings 'all';

my ($epsilon) = 1;

open (EXPECTED, '<', $ARGV[0]) or die "$ARGV[0]: open: $!\n";
open (ACTUAL, '<', $ARGV[1]) or die "$ARGV[1]: open: $!\n";

my ($errors) = 0;
LINE: for (;;) {
    my $a = <EXPECTED>;
    my $b = <ACTUAL>;

    last if !defined $a && !defined $b;
    die "$ARGV[0]:$.: unexpected end of file\n" if !defined $a;
    die "$ARGV[1]:$.: unexpected end of file\n" if !defined $b;

    my (@a) = split (' ', $a);
    my (@b) = split (' ', $b);
    die "$ARGV[1]:$.: contains ". scalar (@b) . " fields but should "
      . "contain " . scalar (@a) . "\n"
	if $#a != $#b;
    foreach my $i (0...$#a) {
	die "$ARGV[1]:$.: unexpected number of decimals\n"
	  if count_decimals ($a[$i]) != count_decimals ($b[$i]);

	my ($an) = to_int ($a[$i]);
	my ($bn) = to_int ($b[$i]);
	if ($an ne $bn && ($bn < $an - $epsilon || $bn > $an + $epsilon)) {
	    $errors++;
	    if ($errors > 5) {
		print "$ARGV[1]: Additional differences suppressed.\n";
		last LINE;
	    }
	    print "$ARGV[1]:$.: Values differ from $ARGV[0]:$.\n";
	    print "Expected:\n", $a;
	    print "Calculated:\n", $b;
	}
    }
}
exit ($errors > 0);

sub count_decimals {
    my ($s) = @_;
    return length (substr ($s, index ($s, '.')));
}

sub to_int {
    local ($_) = @_;
    s/\.//;
    return $_;
}
