#!/usr/bin/perl
while (<>) {
    if ($ARGV ne $oldargv) {
	$translate = -T $ARGV;
	rename($ARGV, $ARGV . '.bak');
	open(ARGVOUT, ">$ARGV");
	select(ARGVOUT);
	$oldargv = $ARGV;
    }
    if ($translate) {
	chop;
	$_ .= "
";
    }
}
continue {
    print;  # this prints to original filename
}
select(STDOUT);
