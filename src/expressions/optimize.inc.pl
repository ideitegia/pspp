do 'generate.pl';

sub generate_output {
    for my $opname (@order) {
	my ($op) = $ops{$opname};

	if (!$op->{OPTIMIZABLE} || $op->{UNIMPLEMENTED}) {
	    print "case $opname:\n";
	    print "  abort ();\n\n";
	    next;
	}

	my (@decls);
	my ($arg_idx) = 0;
	for my $arg (@{$op->{ARGS}}) {
	    my ($decl);
	    my ($name) = $arg->{NAME};
	    my ($type) = $arg->{TYPE};
	    my ($ctype) = c_type ($type);
	    my ($idx) = $arg->{IDX};
	    if (!defined ($idx)) {
		my ($func) = "get_$type->{ATOM}_arg";
		push (@decls, "${ctype}arg_$name = $func (node, $arg_idx)");
	    } else {
		my ($decl) = "size_t arg_$idx = node->arg_cnt";
		$decl .= " - $arg_idx" if $arg_idx;
		push (@decls, $decl);

		push (@decls, "${ctype}*arg_$name = "
		      . "get_$type->{ATOM}_args "
		      . " (node, $arg_idx, arg_$idx, e)");
	    }
	    $arg_idx++;
	}

	my ($sysmis_cond) = make_sysmis_decl ($op, "node->min_valid");
	push (@decls, $sysmis_cond) if defined $sysmis_cond;

	my (@args);
	for my $arg (@{$op->{ARGS}}) {
	    push (@args, "arg_$arg->{NAME}");
	    if (defined $arg->{IDX}) {
		my ($idx) = "arg_$arg->{IDX}";
		$idx .= " / $arg->{TIMES}" if $arg->{TIMES} != 1;
		push (@args, $idx);
	    }
	}
	for my $aux (@{$op->{AUX}}) {
	    my ($type) = $aux->{TYPE};
	    if ($type->{ROLE} eq 'leaf') {
		my ($func) = "get_$type->{ATOM}_arg";
		push (@args, "$func (node, $arg_idx)");
		$arg_idx++;
	    } elsif ($type->{ROLE} eq 'fixed') {
		push (@args, $type->{FIXED_VALUE});
	    } else {
		die;
	    }
	}

	my ($result) = "eval_$op->{OPNAME} (" . join (', ', @args) . ")";
	if (@decls && defined ($sysmis_cond)) {
	    my ($miss_ret) = $op->{RETURNS}{MISSING_VALUE};
	    push (@decls, c_type ($op->{RETURNS}) . "result = "
		  . "force_sysmis ? $miss_ret : $result");
	    $result = "result";
	}

	print "case $opname:\n";
	my ($alloc_func) = "expr_allocate_$op->{RETURNS}{NAME}";
	if (@decls) {
	    print "  {\n";
	    print "    $_;\n" foreach @decls;
	    print "    return $alloc_func (e, $result);\n";
	    print "  }\n";
	} else {
	    print "  return $alloc_func (e, $result);\n";
	}
	print "\n";
    }
}
