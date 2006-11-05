use warnings;
use strict;

my (@line);
while (<>) {
    if (my ($n) = /^\*(\d+)$/) {
	for (1...$n) {
	    $line[1]++;
	    $line[3] = " $line[3]";
	    print ' ', join ('', @line), "\n";
	}
    } elsif (my ($suffix) = /^\$(.*)$/) {
	for my $c (split ('', $suffix)) {
	    $line[1]++;
	    $line[4] .= $c;
	    print ' ', join ('', @line), "\n";
	}
    } elsif (my ($prefix) = /^\^(.*)$/) {
	for my $c (split ('', $prefix)) {
	    $line[1]++;
	    $line[4] = "$c$line[4]";
	    print ' ', join ('', @line), "\n";
	}
    } else {
	@line = /^([A-Z]+)(\d+)([^"]+")( *)([^%"]*)(%?")$/;
	print " $_";
    }
}
