use warnings;
use strict;

my @values = qw(0 2 9.5 27 271 999.95 2718 9999.995 27182 271828
2718281 2**39 2**333 2**-21 -2 -9.5 -27 -271 -999.95 -2718 -9999.995
-27182 -271828 -2718281 -2**39 -2**333 -2**-21 -0 3.125 31.25 314.125
3141.5 31415.875 314159.25 3141592.625 31415926.5 271828182.25
3214567890.5 31415926535.875 -3.125 -31.375 -314.125 -3141.5
-31415.875 -314159.25 -3141592.625 -31415926.5 -271828182.25
-3214567890.5 -31415926535.875);

print "SET CCA=',,,'.\n";
print "SET CCB='-,[[[,]]],-'.\n";
print "SET CCC='((,[,],))'.\n";
print "SET CCD=',XXX,,-'.\n";
print "SET CCE=',,YYY,-'.\n";
print "INPUT PROGRAM.\n";
print "STRING EXPR(A16).\n";
print map ("COMPUTE NUM=$_.\nCOMPUTE EXPR='$_'.\nEND CASE.\n", @values);
print "END FILE.\n";
print "END INPUT PROGRAM.\n";

print "PRINT OUTFILE='output.txt'/EXPR.\n";
for my $format qw (F COMMA DOT DOLLAR PCT E CCA CCB CCC CCD CCE N Z) {
    for my $d (0...16) {
	my ($min_w);
	if ($format ne 'E') {
	    $min_w = $d + 1;
	    $min_w++ if $format eq 'DOLLAR' || $format eq 'PCT';
	    $min_w = 2 if $min_w == 1 && ($format =~ /^CC/);
	} else {
	    $min_w = $d + 7;
	}
	for my $w ($min_w...40) {
	    my ($f) = "$format$w.$d";
	    print "PRINT OUTFILE='output.txt'/'$f: \"' NUM($f) '\"'.\n";
	}
    }
    print "PRINT SPACE OUTFILE='output.txt'.\n";
}
print "EXECUTE.\n";
