do 'generate.pl';

sub generate_output {
    print "#include \"helpers.h\"\n\n";

    for my $opname (@order) {
	my ($op) = $ops{$opname};
	next if $op->{UNIMPLEMENTED};

	my (@args);
	for my $arg (@{$op->{ARGS}}) {
	    if (!defined $arg->{IDX}) {
		push (@args, c_type ($arg->{TYPE}) . $arg->{NAME});
	    } else {
		push (@args, c_type ($arg->{TYPE}) . "$arg->{NAME}" . "[]");
		push (@args, "size_t $arg->{IDX}");
	    }
	}
	for my $aux (@{$op->{AUX}}) {
	    push (@args, c_type ($aux->{TYPE}) . $aux->{NAME});
	}
	push (@args, "void") if !@args;

	my ($statements) = $op->{BLOCK} || "  return $op->{EXPRESSION};\n";

	print "static inline ", c_type ($op->{RETURNS}), "\n";
	print "eval_$opname (", join (', ', @args), ")\n";
	print "{\n";
	print "$statements";
	print "}\n\n";
    }
}
