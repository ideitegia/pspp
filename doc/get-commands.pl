
#
# Creates Texinfo documentation from the source 

$file = $ARGV[0]; 
open(INFO, $file) || die "Cannot open \"$file\"\n" ;	
print "\@table \@asis\n";
while ($line = <INFO>)
{
	if ( $line =~ /^UNIMPL/ ) 
	{
		@fields = split(/,/,$line); 
		$_ = $fields[0];
		s/^UNIMPL//;
		s/ *\(\"// ;
		s/\"//;
		$command = $_;
		$_=$fields[5];
		s/\"//;
		s/\"\)//;
		s/^ *//;
		chomp;
		$description = $_;
		print "\@item $command\n$description\n\n";
	}
}
print "\@end table\n";
close(INFO);			# Close the file
