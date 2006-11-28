#!/usr/bin/perl
# Creates Texinfo documentation from the source 

$file = $ARGV[0]; 
open(INFO, $file) || die "Cannot open \"$file\"\n" ;	
print "\@c Generated from $file by get-commands.pl\n";
print "\@c Do not modify!\n\n";

print "\@table \@asis\n";
while ($line = <INFO>)
{
    if ( $line =~ /^UNIMPL_CMD/ ) 
    {
	@fields = split(/,/,$line); 
	$_ = $fields[0];
	s/^UNIMPL_CMD//;
	s/ *\(\"// ;
	s/\"//;
	$command = $_;
	$_=$fields[1];
	s/^ *\"//;
	s/\"\)//;
	chomp;
	$description = $_;
	print "\@item $command\n$description\n\n";
    }
}
print "\@end table\n";

print "\@c Local Variables:\n";
print "\@c buffer-read-only: t\n";
close(INFO);			# Close the file
