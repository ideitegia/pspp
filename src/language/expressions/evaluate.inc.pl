do 'generate.pl';

sub generate_output {
    for my $opname (@order) {
	my ($op) = $ops{$opname};

	if ($op->{UNIMPLEMENTED}) {
	    print "case $opname:\n";
	    print "  NOT_REACHED ();\n\n";
	    next;
	}

	my (@decls);
	my (@args);
	for my $arg (@{$op->{ARGS}}) {
	    my ($name) = $arg->{NAME};
	    my ($type) = $arg->{TYPE};
	    my ($c_type) = c_type ($type);
	    my ($idx) = $arg->{IDX};
	    push (@args, "arg_$arg->{NAME}");
	    if (!defined ($idx)) {
		my ($decl) = "${c_type}arg_$name";
		if ($type->{ROLE} eq 'any') {
		    unshift (@decls, "$decl = *--$type->{STACK}");
		} elsif ($type->{ROLE} eq 'leaf') {
		    push (@decls, "$decl = op++->$type->{ATOM}");
		} else {
		    die;
		}
	    } else {
		my ($stack) = $type->{STACK};
		defined $stack or die;
		unshift (@decls,
			 "$c_type*arg_$arg->{NAME} = $stack -= arg_$idx");
		unshift (@decls, "size_t arg_$arg->{IDX} = op++->integer");

		my ($idx) = "arg_$idx";
		if ($arg->{TIMES} != 1) {
		    $idx .= " / $arg->{TIMES}";
		}
		push (@args, $idx);
	    }
	}
	for my $aux (@{$op->{AUX}}) {
	    my ($type) = $aux->{TYPE};
	    my ($name) = $aux->{NAME};
	    if ($type->{ROLE} eq 'leaf') {
		my ($c_type) = c_type ($type);
		push (@decls, "${c_type}aux_$name = op++->$type->{ATOM}");
		push (@args, "aux_$name");
	    } elsif ($type->{ROLE} eq 'fixed') {
		push (@args, $type->{FIXED_VALUE});
	    }
	}

	my ($sysmis_cond) = make_sysmis_decl ($op, "op++->integer");
	push (@decls, $sysmis_cond) if defined $sysmis_cond;

	my ($result) = "eval_$op->{OPNAME} (" . join (', ', @args) . ")";

	my ($stack) = $op->{RETURNS}{STACK};

	print "case $opname:\n";
	if (@decls) {
	    print "  {\n";
	    print "    $_;\n" foreach @decls;
	    if (defined $sysmis_cond) {
		my ($miss_ret) = $op->{RETURNS}{MISSING_VALUE};
		print "    *$stack++ = force_sysmis ? $miss_ret : $result;\n";
	    } else {
		print "    *$stack++ = $result;\n";
	    }
	    print "  }\n";
	} else {
	    print "  *$stack++ = $result;\n";
	}
	print "  break;\n\n";
    }
}
