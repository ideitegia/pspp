#!/usr/bin/perl
# Creates Texinfo documentation from the source 

use strict;
use warnings 'all';

my ($file) = $ARGV[0]; 
open(INFO, $file) || die "Cannot open \"$file\"\n" ;	
print "\@c Generated from $file by get-commands.pl\n";
print "\@c Do not modify!\n\n";

print "\@table \@asis\n";
while (<INFO>)
{
    my ($command, $description)
      = /^\s*UNIMPL_CMD\s*\(\s*"([^"]*)"\s*,\s*"([^"]*)"\)\s*$/
	or next;
    print "\@item \@cmd{$command}\n$description\n\n";
}
print "\@end table\n";

print "\@c Local Variables:\n";
print "\@c buffer-read-only: t\n";
print "\@c End:\n";
close(INFO);			# Close the file
