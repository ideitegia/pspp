#! /bin/sh

# Tests BINARY and 360 data file formats.

TEMPDIR=/tmp/pspp-tst-$$

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT
: ${PERL:=perl}

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
     if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR" 
	return ; 
     fi
     cd /
     rm -rf $TEMPDIR
}


fail()
{
    echo $activity
    echo FAILED
    cleanup;
    exit 1;
}


no_result()
{
    echo $activity
    echo NO RESULT;
    cleanup;
    exit 2;
}

pass()
{
    cleanup;
    exit 0;
}

mkdir -p $TEMPDIR



cd $TEMPDIR

activity="create data file"
cat > $TEMPDIR/input.data <<EOF
07-22-2007
10-06-2007
321
07-14-1789
08-26-1789
4
01-01-1972
12-31-1999
682
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program to transform data into 360 formats"
cat > $TEMPDIR/make-360.pl <<'EOF'
use strict;
use warnings;

# ASCII to EBCDIC translation table
our ($ascii2ebcdic) = ""
. "\x00\x01\x02\x03\x37\x2d\x2e\x2f"
. "\x16\x05\x25\x0b\x0c\x0d\x0e\x0f"
. "\x10\x11\x12\x13\x3c\x3d\x32\x26"
. "\x18\x19\x3f\x27\x1c\x1d\x1e\x1f"
. "\x40\x5a\x7f\x7b\x5b\x6c\x50\x7d"
. "\x4d\x5d\x5c\x4e\x6b\x60\x4b\x61"
. "\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7"
. "\xf8\xf9\x7a\x5e\x4c\x7e\x6e\x6f"
. "\x7c\xc1\xc2\xc3\xc4\xc5\xc6\xc7"
. "\xc8\xc9\xd1\xd2\xd3\xd4\xd5\xd6"
. "\xd7\xd8\xd9\xe2\xe3\xe4\xe5\xe6"
. "\xe7\xe8\xe9\xad\xe0\xbd\x9a\x6d"
. "\x79\x81\x82\x83\x84\x85\x86\x87"
. "\x88\x89\x91\x92\x93\x94\x95\x96"
. "\x97\x98\x99\xa2\xa3\xa4\xa5\xa6"
. "\xa7\xa8\xa9\xc0\x4f\xd0\x5f\x07"
. "\x20\x21\x22\x23\x24\x15\x06\x17"
. "\x28\x29\x2a\x2b\x2c\x09\x0a\x1b"
. "\x30\x31\x1a\x33\x34\x35\x36\x08"
. "\x38\x39\x3a\x3b\x04\x14\x3e\xe1"
. "\x41\x42\x43\x44\x45\x46\x47\x48"
. "\x49\x51\x52\x53\x54\x55\x56\x57"
. "\x58\x59\x62\x63\x64\x65\x66\x67"
. "\x68\x69\x70\x71\x72\x73\x74\x75"
. "\x76\x77\x78\x80\x8a\x8b\x8c\x8d"
. "\x8e\x8f\x90\x6a\x9b\x9c\x9d\x9e"
. "\x9f\xa0\xaa\xab\xac\x4a\xae\xaf"
. "\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7"
. "\xb8\xb9\xba\xbb\xbc\xa1\xbe\xbf"
. "\xca\xcb\xcc\xcd\xce\xcf\xda\xdb"
. "\xdc\xdd\xde\xdf\xea\xeb\xec\xed"
. "\xee\xef\xfa\xfb\xfc\xfd\xfe\xff";
length ($ascii2ebcdic) == 256 || die;

open (INPUT, '<', 'input.data') or die "input.data: open: $!\n";
my (@data) = <INPUT> or die;
close (INPUT) or die;
chomp $_ foreach @data;

# Binary mode.
open (OUTPUT, '>', 'binary.bin') or die "binary.bin: create: $!\n";
for $_ (@data) {
    my ($reclen) = pack ("V", length);
    print OUTPUT $reclen, $_, $reclen;
}
close (OUTPUT) or die;

# Fixed mode.
open (OUTPUT, '>', 'fixed.bin') or die "fixed.bin: create: $!\n";
my ($lrecl) = 32;
for $_ (@data) {
    my ($out) = substr ($_, 0, $lrecl);
    $out .= ' ' x ($lrecl - length ($out));
    length ($out) == 32 or die;
    print OUTPUT a2e ($out);
}
close (OUTPUT) or die;

# Variable mode.
open (OUTPUT, '>', 'variable.bin') or die "variable.bin: create: $!\n";
our (@records);
for $_ (@data) {
    push (@records, pack ("n xx", length ($_) + 4) . a2e ($_));
}
dump_records ();
close (OUTPUT) or die;

# Spanned mode.
open (OUTPUT, '>', 'spanned.bin') or die "spanned.bin: create: $!\n";
for my $line (@data) {
    local ($_) = $line;
    my (@r);
    while (length) {
	my ($n) = min (int (rand (5)), length);
	push (@r, substr ($_, 0, $n, ''));
    }
    foreach my $i (0...$#r) {
	my $scc = ($#r == 0 ? 0
		   : $i == 0 ? 1
		   : $i == $#r ? 2
		   : 3);
	push (@records,
	      pack ("nCx", length ($r[$i]) + 4, $scc) . a2e ($r[$i]));
    }
}
dump_records ();
close (OUTPUT) or die;

sub a2e {
    local ($_) = @_;
    my ($s) = "";
    foreach (split (//)) {
        $s .= substr ($ascii2ebcdic, ord, 1);
    }
    return $s;
}

sub min {
    my ($a, $b) = @_;
    return $a < $b ? $a : $b
}

sub dump_records {
    while (@records) {
	my ($n) = min (int (rand (5)) + 1, scalar (@records));
	my (@r) = splice (@records, 0, $n);
	my ($len) = 0;
	$len += length foreach @r;
	print OUTPUT pack ("n xx", $len + 4);
	print OUTPUT foreach @r;
    }
}
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="running make-360.pl"
$PERL make-360.pl
if [ $? -ne 0 ] ; then no_result ; fi

binary_fh='mode=binary'
fixed_fh='mode=360 /recform=fixed /lrecl=32'
variable_fh='mode=360 /recform=variable'
spanned_fh='mode=360 /recform=spanned'

for type in binary fixed variable spanned; do
    activity="create $type.pspp"
    eval fh=\$${type}_fh
    cat > $type.pspp <<EOF
* Read the original file and list its data, to test reading these formats.
file handle input/name='$type.bin'/$fh.
data list fixed file=input notable
	/1 start 1-10 (adate)
	/2 end 1-10 (adate)
	/3 count 1-3.
list.

* Output the data to a new file in the same format.
file handle output/name='${type}2.bin'/$fh.
compute count=count + 1.
print outfile=output/start end count.
execute.

* Re-read the new data and list it, to verify that it was written correctly.
data list fixed file=output notable/
	start 2-11 (adate)
	end 13-22 (adate)
	count 24-26.
list.

EOF
    if [ $? -ne 0 ] ; then no_result ; fi

    # Make sure that pspp.csv isn't left over from another run.
    rm -f pspp.csv

    activity="run $type.pspp"
    $SUPERVISOR $PSPP -o pspp.csv $type.pspp
    if [ $? -ne 0 ] ; then fail ; fi

    activity="compare $type.pspp output"
    diff -c $TEMPDIR/pspp.csv - << EOF
Table: Data List
start,end,count
07/22/2007,10/06/2007,321
07/14/1789,08/26/1789,4
01/01/1972,12/31/1999,682

Table: Data List
start,end,count
07/22/2007,10/06/2007,322
07/14/1789,08/26/1789,5
01/01/1972,12/31/1999,683
EOF
    if [ $? -ne 0 ] ; then fail ; fi
done

pass
