use warnings;

our (@funcs);
our (@vars);
our (%values);

while (<>) {
    chomp;
    s/#.*//;
    next if /^\s*$/;
    my ($dist) = /^(.+):\s*$/ or die;

    @funcs = ();
    @vars = ();
    %values = ('P' => [.01, .1, .2, .3, .4, .5, .6, .7, .8, .9, .99]);
    while (<>) {
	last if /^\s*$/;

	my ($key, $value) = /^\s*(\w+)\s*=\s*(.*)$/;
	my (@values);
	foreach my $s (split (/\s+/, $value)) {
	    if (my ($from, $to, $by) = $s =~ /^(.*):(.*):(.*)$/) {
		for (my ($x) = $from; $x <= $to; $x += $by) {
		    push (@values, sprintf ("%.2f", $x) + 0);
		}
	    } else {
		push (@values, $s);
	    }
	}

	if ($key eq 'funcs') {
	    @funcs = @values;
	} else {
	    push (@vars, $key);
	    $values{$key} = \@values;
	}
    }

    print "DATA LIST LIST/", join (' ', 'P', @vars), ".\n";
    print "NUMERIC ", join (' ', 'x', @funcs), " (F10.4)\n";
    print "COMPUTE x = IDF.$dist (", join (', ', 'P', @vars), ").\n";
    foreach my $func (@funcs) {
	print "COMPUTE $func = $func.$dist (",
	  join (', ', 'x', @vars), ").\n";
    }
    my (@print) = ('P', @vars, 'x', @funcs);
    print "DO IF \$CASENUM = 1.\n";
    print "PRINT OUTFILE='$dist.out'/'", heading (@print), "'\n";
    print "END IF.\n";
    print "PRINT OUTFILE='$dist.out'/",
      join (' ', @print), ".\n";
    print "BEGIN DATA.\n";
    print_all_values (['P', @vars], []);
    print "END DATA.\n";
}

sub print_all_values {
    my (@vars) = @{$_[0]};
    my (@assign) = @{$_[1]};
    if (@vars == @assign) {
	print join (' ', @assign), "\n";
    } else {
	push (@assign, 0);
	my ($var) = $vars[$#assign];
	foreach my $value (@{$values{$var}}) {
	    $assign[$#assign] = $value;
	    print_all_values (\@vars, \@assign);
	}
    }
}

sub heading {
    my (@names) = @_;
    my ($out);
    $out .= pad_to (shift (@names), 8) while $names[0] ne 'x';
    $out .= pad_to (shift (@names), 10) while @names;
    return $out;
}

sub pad_to {
    my ($s, $n) = @_;
    return (' ' x ($n - length ($s))) . $s . ' ';
}
