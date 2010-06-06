use strict;
use warnings 'all';

use Getopt::Long;

# Parse command line.
our ($default_output_file) = $0;
$default_output_file =~ s/\.pl//;
our ($input_file);
our ($output_file);
parse_cmd_line ();

# Initialize type system.
our (%type, @types);
init_all_types ();

# Parse input file.
our (%ops);
our (@funcs, @opers, @order);
parse_input ();

# Produce output.
print_header ();
generate_output ();
print_trailer ();

# Command line.

# Parses the command line.
#
# Initializes $input_file, $output_file.
sub parse_cmd_line {
    GetOptions ("i|input=s" => \$input_file,
		"o|output=s" => \$output_file,
		"h|help" => sub { usage (); })
      or exit 1;

    $input_file = "operations.def" if !defined $input_file;
    $output_file = $default_output_file if !defined $output_file;

    open (INPUT, "<$input_file") or die "$input_file: open: $!\n";
    open (OUTPUT, ">$output_file") or die "$output_file: create: $!\n";

    select (OUTPUT);
}

sub usage {
    print <<EOF;
$0, for generating $default_output_file from definitions
usage: generate.pl [-i INPUT] [-o OUTPUT] [-h]
  -i INPUT    input file containing definitions (default: operations.def)
  -o OUTPUT   output file (default: $default_output_file)
  -h          display this help message
EOF
    exit (0);
}

our ($token);
our ($toktype);

# Types.

# Defines all our types.
#
# Initializes %type, @types.
sub init_all_types {
    # Common user-visible types used throughout evaluation trees.
    init_type ('number', 'any', C_TYPE => 'double',
	       ATOM => 'number', MANGLE => 'n', HUMAN_NAME => 'number',
	       STACK => 'ns', MISSING_VALUE => 'SYSMIS');
    init_type ('string', 'any', C_TYPE => 'struct substring',
	       ATOM => 'string', MANGLE => 's', HUMAN_NAME => 'string',
	       STACK => 'ss', MISSING_VALUE => 'empty_string');
    init_type ('boolean', 'any', C_TYPE => 'double',
	       ATOM => 'number', MANGLE => 'n', HUMAN_NAME => 'boolean',
	       STACK => 'ns', MISSING_VALUE => 'SYSMIS');

    # Format types.
    init_type ('format', 'atom');
    init_type ('ni_format', 'leaf', C_TYPE => 'const struct fmt_spec *',
	       ATOM => 'format', MANGLE => 'f',
	       HUMAN_NAME => 'num_input_format');
    init_type ('no_format', 'leaf', C_TYPE => 'const struct fmt_spec *',
	       ATOM => 'format', MANGLE => 'f',
	       HUMAN_NAME => 'num_output_format');

    # Integer types.
    init_type ('integer', 'leaf', C_TYPE => 'int',
	       ATOM => 'integer', MANGLE => 'n', HUMAN_NAME => 'integer');
    init_type ('pos_int', 'leaf', C_TYPE => 'int',
	       ATOM => 'integer', MANGLE => 'n',
	       HUMAN_NAME => 'positive_integer_constant');

    # Variable names.
    init_type ('variable', 'atom');
    init_type ('num_var', 'leaf', C_TYPE => 'const struct variable *',
	       ATOM => 'variable', MANGLE => 'Vn',
	       HUMAN_NAME => 'num_variable');
    init_type ('str_var', 'leaf', C_TYPE => 'const struct variable *',
	       ATOM => 'variable', MANGLE => 'Vs',
	       HUMAN_NAME => 'string_variable');
    init_type ('var', 'leaf', C_TYPE => 'const struct variable *',
	       ATOM => 'variable', MANGLE => 'V',
	       HUMAN_NAME => 'variable');

    # Vectors.
    init_type ('vector', 'leaf', C_TYPE => 'const struct vector *',
	       ATOM => 'vector', MANGLE => 'v', HUMAN_NAME => 'vector');

    # Fixed types.
    init_type ('expression', 'fixed', C_TYPE => 'struct expression *',
	       FIXED_VALUE => 'e');
    init_type ('case', 'fixed', C_TYPE => 'const struct ccase *',
	       FIXED_VALUE => 'c');
    init_type ('case_idx', 'fixed', C_TYPE => 'size_t',
	       FIXED_VALUE => 'case_idx');
    init_type ('dataset', 'fixed', C_TYPE => 'struct dataset *',
	       FIXED_VALUE => 'ds');

    # One of these is emitted at the end of each expression as a sentinel
    # that tells expr_evaluate() to return the value on the stack.
    init_type ('return_number', 'atom');
    init_type ('return_string', 'atom');

    # Used only for debugging purposes.
    init_type ('operation', 'atom');
}

# init_type has 2 required arguments:
#
#   NAME: Type name.
#
#           `$name' is the type's name in operations.def.
#
#           `OP_$name' is the terminal's type in operations.h.
#
#           `expr_allocate_$name()' allocates a node of the given type.
#
#   ROLE: How the type may be used:
#
#           "any": Usable as operands and function arguments, and
#           function and operator results.
#
#           "leaf": Usable as operands and function arguments, but
#           not function arguments or results.  (Thus, they appear
#           only in leaf nodes in the parse type.)
#
#           "fixed": Not allowed either as an operand or argument
#           type or a result type.  Used only as auxiliary data.
#
#           "atom": Not allowed anywhere; just adds the name to
#           the list of atoms.
#
# All types except those with "atom" as their role also require:
#
#   C_TYPE: The C type that represents this abstract type.
#
# Types with "any" or "leaf" role require:
#
#   ATOM:
#
#           `$atom' is the `struct operation_data' member name.
#
#           get_$atom_name() obtains the corresponding data from a
#           node.
#
#   MANGLE: Short string for name mangling.  Use identical strings
#   if two types should not be overloaded.
#
#   HUMAN_NAME: Name for a type when we describe it to the user.
#
# Types with role "any" require:
#
#   STACK: Name of the local variable in expr_evaluate(), used for
#   maintaining the stack for this type.
#
#   MISSING_VALUE: Expression used for the missing value of this
#   type.
#
# Types with role "fixed" require:
#
#   FIXED_VALUE: Expression used for the value of this type.
sub init_type {
    my ($name, $role, %rest) = @_;
    my ($type) = $type{"\U$name"} = {NAME => $name, ROLE => $role, %rest};

    my (@need_keys) = qw (NAME ROLE);
    if ($role eq 'any') {
	push (@need_keys, qw (C_TYPE ATOM MANGLE HUMAN_NAME STACK MISSING_VALUE));
    } elsif ($role eq 'leaf') {
	push (@need_keys, qw (C_TYPE ATOM MANGLE HUMAN_NAME));
    } elsif ($role eq 'fixed') {
	push (@need_keys, qw (C_TYPE FIXED_VALUE));
    } elsif ($role eq 'atom') {
    } else {
	die "no role `$role'";
    }

    my (%have_keys);
    $have_keys{$_} = 1 foreach keys %$type;
    for my $key (@need_keys) {
	defined $type->{$key} or die "$name lacks $key";
	delete $have_keys{$key};
    }
    scalar (keys (%have_keys)) == 0
      or die "$name has superfluous key(s) " . join (', ', keys (%have_keys));

    push (@types, $type);
}

# c_type(type).
#
# Returns the C type of the given type as a string designed to be
# prepended to a variable name to produce a declaration.  (That won't
# work in general but it works well enough for our types.)
sub c_type {
    my ($type) = @_;
    my ($c_type) = $type->{C_TYPE};
    defined $c_type or die;

    # Append a space unless (typically) $c_type ends in `*'.
    $c_type .= ' ' if $c_type =~ /\w$/;

    return $c_type;
}

# Input parsing.

# Parses the entire input.
#
# Initializes %ops, @funcs, @opers.
sub parse_input {
    get_line ();
    get_token ();
    while ($toktype ne 'eof') {
	my (%op);

	$op{OPTIMIZABLE} = 1;
	$op{UNIMPLEMENTED} = 0;
	$op{EXTENSION} = 0;
	$op{PERM_ONLY} = 0;
	for (;;) {
	    if (match ('extension')) {
		$op{EXTENSION} = 1;
	    } elsif (match ('no_opt')) {
		$op{OPTIMIZABLE} = 0;
	    } elsif (match ('absorb_miss')) {
		$op{ABSORB_MISS} = 1;
	    } elsif (match ('perm_only')) {
		$op{PERM_ONLY} = 1;
	    } elsif (match ('no_abbrev')) {
		$op{NO_ABBREV} = 1;
	    } else {
		last;
	    }
	}

	$op{RETURNS} = parse_type () || $type{NUMBER};
	die "$op{RETURNS} is not a valid return type"
	  if !any ($op{RETURNS}, @type{qw (NUMBER STRING BOOLEAN)});

	$op{CATEGORY} = $token;
	if (!any ($op{CATEGORY}, qw (operator function))) {
	    die "`operator' or `function' expected at `$token'";
	}
	get_token ();

	my ($name) = force ("id");

	die "function name may not contain underscore"
	  if $op{CATEGORY} eq 'function' && $name =~ /_/;
	die "operator name may not contain period"
	  if $op{CATEGORY} eq 'operator' && $name =~ /\./;

	if (my ($prefix, $suffix) = $name =~ /^(.*)\.(\d+)$/) {
	    $name = $prefix;
	    $op{MIN_VALID} = $suffix;
	    $op{ABSORB_MISS} = 1;
	}
	$op{NAME} = $name;

	force_match ('(');
	@{$op{ARGS}} = ();
	while (!match (')')) {
	    my ($arg) = parse_arg ();
	    push (@{$op{ARGS}}, $arg);
	    if (defined ($arg->{IDX})) {
		last if match (')');
		die "array must be last argument";
	    }
	    if (!match (',')) {
		force_match (')');
		last;
	    }
	}

	for my $arg (@{$op{ARGS}}) {
	    next if !defined $arg->{CONDITION};
	    my ($any_arg) = join ('|', map ($_->{NAME}, @{$op{ARGS}}));
	    $arg->{CONDITION} =~ s/\b($any_arg)\b/arg_$1/g;
	}

	my ($opname) = "OP_$op{NAME}";
	$opname =~ tr/./_/;
	if ($op{CATEGORY} eq 'function') {
	    my ($mangle) = join ('', map ($_->{TYPE}{MANGLE}, @{$op{ARGS}}));
	    $op{MANGLE} = $mangle;
	    $opname .= "_$mangle";
	}
	$op{OPNAME} = $opname;

	if ($op{MIN_VALID}) {
	    my ($array_arg) = array_arg (\%op);
	    die "can't have minimum valid count without array arg"
	      if !defined $array_arg;
	    die "minimum valid count allowed only with double array"
	      if $array_arg->{TYPE} ne $type{NUMBER};
	    die "can't have minimum valid count if array has multiplication factor"
	      if $array_arg->{TIMES} != 1;
	}

	while ($toktype eq 'id') {
	    my ($type) = parse_type () or die "parse error";
	    die "`$type->{NAME}' is not allowed as auxiliary data"
	      unless $type->{ROLE} eq 'leaf' || $type->{ROLE} eq 'fixed';
	    my ($name) = force ("id");
	    push (@{$op{AUX}}, {TYPE => $type, NAME => $name});
	    force_match (';');
	}

	if ($op{OPTIMIZABLE}) {
	    die "random variate functions must be marked `no_opt'"
	      if $op{NAME} =~ /^RV\./;
	    for my $aux (@{$op{AUX}}) {
		if (any ($aux->{TYPE}, @type{qw (CASE CASE_IDX)})) {
		    die "operators with $aux->{TYPE} aux data must be "
		      . "marked `no_opt'";
		}
	    }
	}

	if ($op{RETURNS} eq $type{STRING} && !defined ($op{ABSORB_MISS})) {
	    my (@args);
	    for my $arg (@{$op{ARGS}}) {
		if (any ($arg->{TYPE}, @type{qw (NUMBER BOOLEAN)})) {
		    die "$op{NAME} returns string and has double or bool "
		      . "argument, but is not marked ABSORB_MISS";
		}
		if (defined $arg->{CONDITION}) {
		    die "$op{NAME} returns string but has argument with condition";
		}
	    }
	}

	if ($toktype eq 'block') {
	    $op{BLOCK} = force ('block');
	} elsif ($toktype eq 'expression') {
	    if ($token eq 'unimplemented') {
		$op{UNIMPLEMENTED} = 1;
	    } else {
		$op{EXPRESSION} = $token;
	    }
	    get_token ();
	} else {
	    die "block or expression expected";
	}

	die "duplicate operation name $opname" if defined $ops{$opname};
	$ops{$opname} = \%op;
	if ($op{CATEGORY} eq 'function') {
	    push (@funcs, $opname);
	} else {
	    push (@opers, $opname);
	}
    }
    close(INPUT);

    @funcs = sort {$ops{$a}->{NAME} cmp $ops{$b}->{NAME}
		     ||
		       $ops{$a}->{OPNAME} cmp $ops{$b}->{OPNAME}}
      @funcs;
    @opers = sort {$ops{$a}->{NAME} cmp $ops{$b}->{NAME}} @opers;
    @order = (@funcs, @opers);
}

# Reads the next token into $token, $toktype.
sub get_token {
    our ($line);
    lookahead ();
    return if defined ($toktype) && $toktype eq 'eof';
    $toktype = 'id', $token = $1, return
	if $line =~ /\G([a-zA-Z_][a-zA-Z_.0-9]*)/gc;
    $toktype = 'int', $token = $1, return if $line =~ /\G([0-9]+)/gc;
    $toktype = 'punct', $token = $1, return if $line =~ /\G([][(),*;.])/gc;
    if ($line =~ /\G=/gc) {
	$toktype = "expression";
	$line =~ /\G\s+/gc;
	$token = accumulate_balanced (';');
    } elsif ($line =~ /\G\{/gc) {
	$toktype = "block";
	$token = accumulate_balanced ('}');
	$token =~ s/^\n+//;
    } else {
	die "bad character `" . substr ($line, pos $line, 1) . "' in input";
    }
}

# Skip whitespace, then return the remainder of the line.
sub lookahead {
    our ($line);
    die "unexpected end of file" if !defined ($line);
    for (;;) {
	$line =~ /\G\s+/gc;
	last if pos ($line) < length ($line);
	get_line ();
	$token = $toktype = 'eof', return if !defined ($line);
    }
    return substr ($line, pos ($line));
}

# accumulate_balanced($chars)
#
# Accumulates input until a character in $chars is encountered, except
# that balanced pairs of (), [], or {} cause $chars to be ignored.
#
# Returns the input read.
sub accumulate_balanced {
    my ($end) = @_;
    my ($s) = "";
    my ($nest) = 0;
    our ($line);
    for (;;) {
	my ($start) = pos ($line);
	if ($line =~ /\G([^][(){};,]*)([][(){};,])/gc) {
	    $s .= substr ($line, $start, pos ($line) - $start - 1)
		if pos ($line) > $start;
	    my ($last) = substr ($line, pos ($line) - 1, 1);
	    if ($last =~ /[[({]/) {
		$nest++;
		$s .= $last;
	    } elsif ($last =~ /[])}]/) {
		if ($nest > 0) {
		    $nest--;
		    $s .= $last;
		} elsif (index ($end, $last) >= 0) {
		    return $s;
		} else {
		    die "unbalanced parentheses";
		}
	    } elsif (index ($end, $last) >= 0) {
		return $s if !$nest;
		$s .= $last;
	    } else {
		$s .= $last;
	    }
	} else {
	    $s .= substr ($line, pos ($line)) . "\n";
	    get_line ();
	}
    }
}

# Reads the next line from INPUT into $line.
sub get_line {
    our ($line);
    $line = <INPUT>;
    if (defined ($line)) {
	chomp $line;
	$line =~ s%//.*%%;
	pos ($line) = 0;
    }
}

# If the current token is an identifier that names a type,
# returns the type and skips to the next token.
# Otherwise, returns undef.
sub parse_type {
    if ($toktype eq 'id') {
	foreach my $type (values (%type)) {
	    get_token (), return $type
	      if defined ($type->{NAME}) && $type->{NAME} eq $token;
	}
    }
    return;
}

# force($type).
#
# Makes sure that $toktype equals $type, reads the next token, and
# returns the previous $token.
sub force {
    my ($type) = @_;
    die "parse error at `$token' expecting $type"
	if $type ne $toktype;
    my ($tok) = $token;
    get_token ();
    return $tok;
}

# force($tok).
#
# If $token equals $tok, reads the next token and returns true.
# Otherwise, returns false.
sub match {
    my ($tok) = @_;
    if ($token eq $tok) {
	get_token ();
	return 1;
    } else {
	return 0;
    }
}

# force_match($tok).
#
# If $token equals $tok, reads the next token.
# Otherwise, flags an error in the input.
sub force_match {
    my ($tok) = @_;
    die "parse error at `$token' expecting `$tok'" if !match ($tok);
}

# Parses and returns a function argument.
sub parse_arg {
    my (%arg);
    $arg{TYPE} = parse_type () || $type{NUMBER};
    die "argument name expected at `$token'" if $toktype ne 'id';
    $arg{NAME} = $token;

    if (lookahead () =~ /^[[,)]/) {
	get_token ();
	if (match ('[')) {
	    die "only double and string arrays supported"
	      if !any ($arg{TYPE}, @type{qw (NUMBER STRING)});
	    $arg{IDX} = force ('id');
	    if (match ('*')) {
		$arg{TIMES} = force ('int');
		die "multiplication factor must be positive"
		  if $arg{TIMES} < 1;
	    } else {
		$arg{TIMES} = 1;
	    }
	    force_match (']');
	}
    } else {
	$arg{CONDITION} = $arg{NAME} . ' ' . accumulate_balanced (',)');
	our ($line);
	pos ($line) -= 1;
	get_token ();
    }
    return \%arg;
}

# Output.

# Prints the output file header.
sub print_header {
    print <<EOF;
/* $output_file
   Generated from $input_file by generate.pl.  
   Do not modify! */

EOF
}

# Prints the output file trailer.
sub print_trailer {
    print <<EOF;

/*
   Local Variables:
   mode: c
   buffer-read-only: t
   End:
*/
EOF
}

# Utilities.

# any($target, @list)
#
# Returns true if $target appears in @list,
# false otherwise.
sub any {
    $_ eq $_[0] and return 1 foreach @_[1...$#_];
    return 0;
}

# make_sysmis_decl($op, $min_valid_src)
#
# Returns a declaration for a boolean variable called `force_sysmis',
# which will be true when operation $op should be system-missing.
# Returns undef if there are no such circumstances.
#
# If $op has a minimum number of valid arguments, $min_valid_src
# should be an an expression that evaluates to the minimum number of
# valid arguments for $op.
sub make_sysmis_decl {
    my ($op, $min_valid_src) = @_;
    my (@sysmis_cond); 
    if (!$op->{ABSORB_MISS}) {
	for my $arg (@{$op->{ARGS}}) {
	    my ($arg_name) = "arg_$arg->{NAME}";
	    if (!defined $arg->{IDX}) {
		if (any ($arg->{TYPE}, @type{qw (NUMBER BOOLEAN)})) {
		    push (@sysmis_cond, "!is_valid ($arg_name)");
		}
	    } elsif ($arg->{TYPE} eq $type{NUMBER}) {
		my ($a) = "$arg_name";
		my ($n) = "arg_$arg->{IDX}";
		push (@sysmis_cond, "count_valid ($a, $n) < $n");
	    }
	}
    } elsif (defined $op->{MIN_VALID}) {
	my ($args) = $op->{ARGS};
	my ($arg) = ${$args}[$#{$args}];
	my ($a) = "arg_$arg->{NAME}";
	my ($n) = "arg_$arg->{IDX}";
	push (@sysmis_cond, "count_valid ($a, $n) < $min_valid_src");
    }
    for my $arg (@{$op->{ARGS}}) {
	push (@sysmis_cond, "!($arg->{CONDITION})")
	  if defined $arg->{CONDITION};
    }
    return "bool force_sysmis = " . join (' || ', @sysmis_cond)
      if @sysmis_cond;
    return;
}

# array_arg($op)
#
# If $op has an array argument, return it.
# Otherwise, returns undef.
sub array_arg {
    my ($op) = @_;
    my ($args) = $op->{ARGS};
    return if !@$args;
    my ($last_arg) = $args->[@$args - 1];
    return $last_arg if defined $last_arg->{IDX};
    return;
}
