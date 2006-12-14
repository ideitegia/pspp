#! /usr/bin/perl -w

use strict;
use Getopt::Long;

my $exact = 0;
my $spss = 0;
my $verbose = 0;
Getopt::Long::Configure ("bundling");
GetOptions ("e|exact!" => \$exact,
	    "s|spss!" => \$spss,
	    "v|verbose+" => \$verbose,
	    "h|help" => sub { usage (0) })
  or usage (1);

sub usage {
    print "$0: compare expected and actual numeric formatting output\n";
    print "usage: $0 [OPTION...] EXPECTED ACTUAL\n";
    print "where EXPECTED is the file containing expected output\n";
    print "and ACTUAL is the file containing actual output.\n";
    print "Options:\n";
    print "  -e, --exact: Require numbers to be exactly equal.\n";
    print "               (By default, small differences are permitted.)\n";
    print "  -s, --spss: Ignore most SPSS formatting bugs in EXPECTED.\n";
    print "              (A few differences are not compensated)\n";
    print "  -v, --verbose: Use once to summarize errors and differences.\n";
    print "                 Use twice for details of differences.\n";
    exit (@_);
}

open (EXPECTED, '<', $ARGV[0]) or die "$ARGV[0]: open: $!\n";
open (ACTUAL, '<', $ARGV[1]) or die "$ARGV[1]: open: $!\n";
my ($expr);
my ($bad_round) = 0;
my ($approximate) = 0;
my ($spss_wtf1) = 0;
my ($spss_wtf2) = 0;
my ($lost_sign) = 0;
my ($errors) = 0;
while (defined (my $a = <EXPECTED>) && defined (my $b = <ACTUAL>)) {
    chomp $a;
    chomp $b;
    if ($a eq $b) {
	if ($a !~ /^\s*$/ && $a !~ /:/) {
	    $expr = $a;
	    $expr =~ s/\s*$//;
	    $expr =~ s/^\s*//;
	}
    } else {
	my ($fmt, $a_out) = $a =~ /^ (.*): "(.*)"$/ or die;
	my ($b_fmt, $b_out) = $b =~ /^ (.*): "(.*)"$/ or die;
	die if $fmt ne $b_fmt;
	die if $a_out eq $b_out;

	if (!$exact) {
	    if (increment ($a_out) eq $b_out || increment ($b_out) eq $a_out) {
		$approximate++;
		next;
	    }
	}
	if ($spss) {
	    if ($a_out =~ /0.*0/ && $a_out !~ /[1-9]/) {
		$bad_round++;
		next;
	    } elsif ($a_out =~ /\*/ && $a_out !~ /^\*+$/) {
		$spss_wtf1++;
		next;
	    } elsif ($expr =~ /^-/
		     && $a_out =~ /^\*+$/
		     && $b_out =~ /-\d(\.\d*#*)?E[-+]\d\d\d/
		     && $fmt =~ /^E/) {
		$spss_wtf2++;
		next;
	    } elsif ($expr =~ /^-/
		     && (($a_out !~ /-/ && $a_out =~ /[1-9]/ && $b_out =~ /-/)
			 || ($a_out =~ /^[0-9]+$/ && $b_out =~ /^\*+$/))) {
		$lost_sign++;
		next;
	    }
	}
	print "$.: $expr in $fmt: expected \"$a_out\", got \"$b_out\"\n"
	  if $verbose > 1;
	$errors++;
    }
}
while (<EXPECTED>) {
    print "Extra lines in $ARGV[0]\n";
    $errors++;
    last;
}
while (<ACTUAL>) {
    print "Extra lines in $ARGV[1]\n";
    $errors++;
    last;
}
if ($verbose) {
    print "$errors errors\n";
    if (!$exact) {
	print "$approximate approximate matches\n";
    }
    if ($spss) {
	print "$bad_round bad rounds\n";
	print "$spss_wtf1 SPSS WTF 1\n";
	print "$spss_wtf2 SPSS WTF 2\n";
	print "$lost_sign lost signs\n";
    }
}
exit ($errors > 0);

# Returns the argument value incremented by one unit in its final
# decimal place.
sub increment {
    local ($_) = @_;
    my ($last_digit, $i);
    for ($i = 0; $i < length $_; $i++) {
	my ($c) = substr ($_, $i, 1);
	last if ($c eq 'E');
	$last_digit = $i if $c =~ /[0-9]/;
    }
    return $_ if !defined $last_digit;
    for ($i = $last_digit; $i >= 0; $i--) {
	my ($c) = substr ($_, $i, 1);
	if ($c eq '9') {
	    substr ($_, $i, 1) = '0';
	} elsif ($c =~ /[0-8]/) {
	    substr ($_, $i, 1) = chr (ord ($c) + 1);
	    last;
	}
    }
    $_ = "1$_" if $i < 0;
    return $_;
}
