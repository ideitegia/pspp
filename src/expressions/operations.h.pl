#!/usr/bin/perl

use PSPP_expressions ;
#
# Produce output.
print_header ();
generate_output ();
print_trailer ();


sub generate_output {
    print "#include <stdlib.h>\n";
    print "#include \"bool.h\"\n\n";

    print "typedef enum";
    print "  {\n";
    my (@atoms);
    foreach my $type (@types) {
	next if $type->{ROLE} eq 'fixed';
	push (@atoms, "OP_$type->{NAME}");
    }
    print_operations ('atom', 1, \@atoms);
    print_operations ('function', "OP_atom_last + 1", \@funcs);
    print_operations ('operator', "OP_function_last + 1", \@opers);
    print_range ("OP_composite", "OP_function_first", "OP_operator_last");
    print ",\n\n";
    print_range ("OP", "OP_atom_first", "OP_composite_last");
    print "\n  }\n";
    print "operation_type, atom_type;\n";

    print_predicate ('is_operation', 'OP');
    print_predicate ("is_$_", "OP_$_")
	foreach qw (atom composite function operator);
}

sub print_operations {
    my ($type, $first, $names) = @_;
    print "    /* \u$type types. */\n";
    print "    $names->[0] = $first,\n";
    print "    $_,\n" foreach @$names[1...$#{$names}];
    print_range ("OP_$type", $names->[0], $names->[$#{$names}]);
    print ",\n\n";
}

sub print_range {
    my ($prefix, $first, $last) = @_;
    print "    ${prefix}_first = $first,\n";
    print "    ${prefix}_last = $last,\n";
    print "    ${prefix}_cnt = ${prefix}_last - ${prefix}_first + 1";
}

sub print_predicate {
    my ($function, $category) = @_;
    my ($assertion) = "";

    print "\nstatic inline bool\n";
    print "$function (operation_type op)\n";
    print "{\n";
    print "  assert (is_operation (op));\n" if $function ne 'is_operation';
    print "  return op >= ${category}_first && op <= ${category}_last;\n";
    print "}\n";
}
