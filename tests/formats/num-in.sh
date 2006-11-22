#! /bin/sh

TEMPDIR=/tmp/pspp-tst-$$
mkdir -p $TEMPDIR
trap 'cd /; rm -rf $TEMPDIR' 0

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH

fail()
{
    echo $activity
    echo FAILED
    exit 1;
}


no_result()
{
    echo $activity
    echo NO RESULT;
    exit 2;
}

pass()
{
    exit 0;
}

cd $TEMPDIR

activity="write Perl program"
cat > num-in.pl <<'EOF'
#! /usr/bin/perl

use POSIX;
use strict;
use warnings;

our $next = 0;

for my $number (0, 1, .5, .015625, 123) {
    my ($base_exp) = floor ($number ? log10 ($number) : 0);
    for my $offset (-3...3) {
	my ($exponent) = $base_exp + $offset;
	my ($fraction) = $number / 10**$offset;

	permute_zeros ($fraction, $exponent);
    }
}

sub permute_zeros {
    my ($fraction, $exponent) = @_;

    my ($frac_rep) = sprintf ("%f", $fraction);
    my ($leading_zeros) = length (($frac_rep =~ /^(0*)/)[0]);
    my ($trailing_zeros) = length (($frac_rep =~ /(\.?0*)$/)[0]);
    for my $i (0...$leading_zeros) {
	for my $j (0...$trailing_zeros) {
	    my ($trimmed) = substr ($frac_rep, $i,
				    length ($frac_rep) - $i - $j);
	    next if $trimmed eq '.' || $trimmed eq '';

	    permute_commas ($trimmed, $exponent);
	}
    }
}

sub permute_commas {
    my ($frac_rep, $exponent) = @_;
    permute_dot_comma ($frac_rep, $exponent);
    my ($pos) = int (my_rand (length ($frac_rep) + 1));
    $frac_rep = substr ($frac_rep, 0, $pos) . "," . substr ($frac_rep, $pos);
    permute_dot_comma ($frac_rep, $exponent);
}

sub permute_dot_comma {
    my ($frac_rep, $exponent) = @_;
    permute_exponent_syntax ($frac_rep, $exponent);
    if ($frac_rep =~ /[,.]/) {
	$frac_rep =~ tr/.,/,./;
	permute_exponent_syntax ($frac_rep, $exponent);
    }
}

sub permute_exponent_syntax {
    my ($frac_rep, $exponent) = @_;
    my (@exp_reps);
    if ($exponent == 0) {
	@exp_reps = pick ('', 'e0', 'e-0', 'e+0', '-0', '+0');
    } elsif ($exponent > 0) {
	@exp_reps = pick ("e$exponent", "e+$exponent", "+$exponent");
    } else {
	my ($abs_exp) = -$exponent;
	@exp_reps = pick ("e-$abs_exp", , "e-$abs_exp", "-$abs_exp");
    }
    permute_sign_and_affix ($frac_rep, $_) foreach @exp_reps;
}

sub permute_sign_and_affix {
    my ($frac_rep, $exp_rep) = @_;
    for my $prefix (pick ('', '$'),
		    pick ('-', '-$', '$-', '$-$'),
		    pick ('+', '+$', '$+', '$+$')) {
	for my $suffix ('', '%') {
	    permute_spaces ("$prefix$frac_rep$exp_rep$suffix");
	}
    }
}

sub permute_spaces {
    my ($s) = @_;
    $s =~ s/([-+\$e%])/ $1 /g;
    my (@fields) = split (' ', $s);
    print join ('', @fields), "\n";

    if ($#fields > 0) {
	my ($pos) = int (my_rand ($#fields)) + 1;
	print join ('', @fields[0...$pos - 1]);
	print " ";
	print join ('', @fields[$pos...$#fields]);
	print "\n";
    }
}

sub pick {
    return $_[int (my_rand ($#_ + 1))];
}

sub my_rand {
    my ($modulo) = @_;
    $next = ($next * 1103515245 + 12345) % (2**32);
    return int ($next / 65536) % $modulo;
}
EOF

activity="generate data"
$PERL num-in.pl > num-in.data
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="generate pspp syntax"
cat > num-in.pspp <<EOF
SET ERRORS=NONE.
SET MXERRS=10000000.
SET MXWARNS=10000000.
DATA LIST FILE='num-in.data' /
	f 1-40 (f)
	comma 1-40 (comma)
	dot 1-40 (dot)
	dollar 1-40 (dollar)
	pct 1-40 (pct)
	e 1-40 (e).
PRINT OUTFILE='num-in.out'/all (6f10.4).
EXECUTE.
EOF
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="run program"
$SUPERVISOR $PSPP --testing-mode num-in.pspp
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="gunzip expected results"
gzip -cd < $top_srcdir/tests/formats/num-in.expected.gz > num-in.expected
if [ $? -ne 0 ] ; then no_result ; fi
echo -n .

activity="compare output"
diff -u num-in.expected num-in.out
if [ $? -ne 0 ] ; then fail ; fi

echo .

pass
