#! /bin/sh

# Tests the expression optimizer and evaluator.

TEMPDIR=/tmp/pspp-tst-$$

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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
activity="create expressions list"
sed -ne 's/#.*//;/^[ 	]*$/!p' > $TEMPDIR/expr-list <<'EOF'

# Number syntax.
1e2 => 100.00
1e+2 => 100.00
1e-2 => 0.01
1e-99 => 0.00

# Test using numeric/string values as Booleans and vice-versa
0 AND 1 => false
$true AND 1 => true
1 OR $false => true
1 OR $sysmis => true
2 OR $sysmis => sysmis
2 AND $sysmis => false
'string' AND $sysmis => error
0 AND $sysmis => false
(1>2) + 1 => 1.00
$true + $false => 1.00

# Addition and subtraction.
1 + 2 => 3.00
1 + $true => 2.00
$sysmis + 1 => sysmis
7676 + $sysmis => sysmis
('foo') + 5 => error
('foo') + ('bar') => error	# Arithmetic concatenation requires CONCAT.
'foo' + 'bar' => "foobar"	# Lexical concatentation succeeds.
1 +3 - 2 +4 -5 => 1.00
1 - $true => 0.00
$true - 4/3 => -0.33
'string' - 1e10 => error
9.5 - '' => error
1 - 2 => -1.00
52 -23 => 29.00 

# Multiplication and division
5 * 10 => 50.00
10 * $true => 10.00
$true * 5 => 5.00
1.5 * $true => 1.50
5 * $sysmis => sysmis
$sysmis * 15 => sysmis
2 * 5 / 10 => 1.00
1 / 2 => 0.50
2 / 5 => 0.40
12 / 3 / 2 => 2.00

# Exponentiation.
2**8 => 256.00
(2**3)**4 => 4096.00	# Irritating, but compatible.
2**3**4 => 4096.00

# Unary minus.
2+-3 => -1.00
2*-3 => -6.00
-3**2 => -9.00
(-3)**2 => 9.00
2**-1 => 0.50
0**0 => sysmis
0**-1 => sysmis
(-3)**1.5 => sysmis

# AND truth table.
$false AND $false => false
$false AND $true => false
$false AND $sysmis => false
$true AND $false => false
$true AND $true => true
$true AND $sysmis => sysmis
$sysmis AND $false => false
$sysmis AND $true => sysmis
$sysmis AND $sysmis => sysmis
$false & $false => false
$false & $true => false
$false & $sysmis => false
$true & $false => false
$true & $true => true
$true & $sysmis => sysmis
$sysmis & $false => false
$sysmis & $true => sysmis
$sysmis & $sysmis => sysmis

# OR truth table.
$false OR $false => false
$false OR $true => true
$false OR $sysmis => sysmis
$true OR $false => true
$true OR $true => true
$true OR $sysmis => true
$sysmis OR $false => sysmis
$sysmis OR $true => true
$sysmis OR $sysmis => sysmis
$false | $false => false
$false | $true => true
$false | $sysmis => sysmis
$true | $false => true
$true | $true => true
$true | $sysmis => true
$sysmis | $false => sysmis
$sysmis | $true => true
$sysmis | $sysmis => sysmis

# NOT truth table.
not $false => true
not 0 => true
not 2.5 => true
not $true => false
not 1 => false
not $sysmis => sysmis
~ $false => true
~ 0 => true
~ 2.5 => true
~ $true => false
~ 1 => false
~ $sysmis => sysmis

# Relational operators.
1 eq 1 => true
1 = 1 => true
1 eq 2 => false
2 = 3 => false
1 eq 'foobar' => error
5 eq 'foobar' => error
'baz' = 10 => error
'quux' = 5.55 => error
'foobar' = 'foobar' => true
'quux' = 'bar' => false
'bar   ' = 'bar' => true
'asdf         ' = 'asdf  ' => true
'asdfj   ' = 'asdf' => false
1 + 2 = 3 => true		# Check precedence.
1 >= 2 = 2 ge 3 => false	# Check precedence.
3 ne 2 ~= 1 => false		# Mathematically true.
3 > 2 > 1 => false		# Mathematically true.

1 <= 2 => true
2.5 <= 1.5 => false
1 le 2 => true
2 <= 2 => true
2 le 2 => true
2 < = 2 => error	# Make sure <= token can't be split.
1 <= 'foobar' => error
5 <= 'foobar' => error
'baz' <= 10 => error
'quux' <= 5.55 => error
'0123' <= '0123' => true
'0123' <= '0124' => true
'0124' le '0123' => false
'0123  ' <= '0123' => true
'0123' le '0123  ' => true

1 < 2 => true
2.5 < 1.5 => false
3.5 lt 4 => true
4 lt 3.5 => false
1 lt 'foobar' => error
5 lt 'foobar' => error
'baz' < 10 => error
'quux' < 5.55 => error
'0123' lt '0123' => false
'0123' < '0124' => true
'0124' lt '0123' => false
'0123  ' < '0123' => false
'0123' lt '0123  ' => false

1 >= 2 => false
2.5 >= 1.5 => true
1 ge 2 => false
2 >= 2 => true
2 ge 2 => true
2 > = 2 => error	# Make sure >= token can't be split.
1 >= 'foobar' => error
5 ge 'foobar' => error
'baz' ge 10 => error
'quux' >= 5.55 => error
'0123' ge '0123' => true
'0123' >= '0124' => false
'0124' >= '0123' => true
'0123  ' ge '0123' => true
'0123' >= '0123  ' => true

1 > 2 => false
2.5 > 1.5 => true
3.5 gt 4 => false
4 gt 3.5 => true
1 gt 'foobar' => error
5 gt 'foobar' => error
'baz' > 10 => error
'quux' > 5.55 => error
'0123' gt '0123' => false
'0123' > '0124' => false
'0124' gt '0123' => true
'0123  ' > '0123' => false
'0123' gt '0123  ' => false

1 ne 1 => false
1 ~= 1 => false
1 <> 2 => true
2 ne 3 => true
1 ~= 'foobar' => error
5 <> 'foobar' => error
'baz' ne 10 => error
'quux' ~= 5.55 => error
'foobar' <> 'foobar' => false
'quux' ne 'bar' => true
'bar   ' <> 'bar' => false
'asdf         ' ~= 'asdf  ' => false
'asdfj   ' ne 'asdf' => true
1 < > 1 => error	# <> token can't be split
1 ~ = 1 => error	# ~= token can't be split

exp(10) => 22026.47
exp('x') => error

lg10(500) => 2.70
lg10('x') => error

ln(10) => 2.30
ln('x') => error

sqrt(500) => 22.36
sqrt('x') => error

abs(-10.5) => 10.50
abs(-55.79) => 55.79
abs(22) => 22.00
abs(0) => 0.00

mod(55.5, 2) => 1.50
mod(-55.5, 2) => -1.50
mod(55.5, -2) => 1.50
mod(-55.5, -2) => -1.50
mod('a', 2) => error
mod(2, 'a') => error
mod('a', 'b') => error

mod10(55.5) => 5.50
mod10(-55.5) => -5.50
mod10('x') => error

rnd(5.4) => 5.00
rnd(5.6) => 6.00
rnd(-5.4) => -5.00
rnd(-5.6) => -6.00
rnd('x') => error

trunc(1.2) => 1.00
trunc(1.9) => 1.00
trunc(-1.2) => -1.00
trunc(-1.9) => -1.00
trunc('x') => error

acos(.5) / 3.14159 * 180 => 60.00
arcos(.75) / 3.14159 * 180 => 41.41
arcos(-.5) / 3.14159 * 180 => 120.00
acos(-.75) / 3.14159 * 180 => 138.59
acos(-1) / 3.14159 * 180 => 180.00
arcos(1) / 3.14159 * 180 => 0.00
acos(-1.01) => sysmis
arcos(1.01) => sysmis
acos('x') => error

arsin(.5) / 3.14159 * 180 => 30.00
asin(.25) / 3.14159 * 180 => 14.48
arsin(-.5) / 3.14159 * 180 => -30.00
asin(-.25) / 3.14159 * 180 => -14.48
arsin(-1.01) => sysmis
asin(1.01) => sysmis
arsin('x') => error

artan(1) / 3.14159 * 180 => 45.00
atan(10) / 3.14159 * 180 => 84.29
artan(-1) / 3.14159 * 180 => -45.00
atan(-10) / 3.14159 * 180 => -84.29
artan('x') => error

cos(60 / 180 * 3.14159) => 0.50
cos(45 / 180 * 3.14159) => 0.71
cos(30 / 180 * 3.14159) => 0.87
cos(15 / 180 * 3.14159) => 0.97
cos(-60 / 180 * 3.14159) => 0.50
cos(-45 / 180 * 3.14159) => 0.71
cos(-30 / 180 * 3.14159) => 0.87
cos(-15 / 180 * 3.14159) => 0.97
cos(123 / 180 * 3.14159) => -0.54
cos(321 / 180 * 3.14159) => 0.78
cos('x') => error

sin(60 / 180 * 3.14159) => 0.87
sin(45 / 180 * 3.14159) => 0.71
sin(30 / 180 * 3.14159) => 0.50
sin(15 / 180 * 3.14159) => 0.26
sin(-60 / 180 * 3.14159) => -0.87
sin(-45 / 180 * 3.14159) => -0.71
sin(-30 / 180 * 3.14159) => -0.50
sin(-15 / 180 * 3.14159) => -0.26
sin(123 / 180 * 3.14159) => 0.84
sin(321 / 180 * 3.14159) => -0.63
sin('x') => error

tan(60 / 180 * 3.14159) => 1.73
tan(45 / 180 * 3.14159) => 1.00
tan(30 / 180 * 3.14159) => 0.58
tan(15 / 180 * 3.14159) => 0.27
tan(-60 / 180 * 3.14159) => -1.73
tan(-45 / 180 * 3.14159) => -1.00
tan(-30 / 180 * 3.14159) => -0.58
tan(-15 / 180 * 3.14159) => -0.27
tan(123 / 180 * 3.14159) => -1.54
tan(321 / 180 * 3.14159) => -0.81
tan('x') => error

# FIXME: a variable name as the argument to SYSMIS is a special case
# that we don't yet test.  We also can't test VALUE this way.
missing(10) => false
missing($sysmis) => true
missing(asin(1.01)) => true
missing(asin(.5)) => false
missing('    ') => error
nmiss($sysmis) => 1.00
nmiss(0) => 0.00
nmiss($sysmis, $sysmis, $sysmis) => 3.00
nmiss(1, 2, 3, 4) => 0.00
nmiss(1, $sysmis, $sysmis, 2, 2, $sysmis, $sysmis, 3, 4) => 4.00
nvalid($sysmis) => 0.00
nvalid(0) => 1.00
nvalid($sysmis, $sysmis, $sysmis) => 0.00
nvalid(1, 2, 3, 4) => 4.00
nvalid(1, $sysmis, $sysmis, 2, 2, $sysmis, $sysmis, 3, 4) => 5.00
sysmis(10) => false
sysmis($sysmis) => true
sysmis(asin(1.01)) => true
sysmis(asin(.5)) => false
sysmis('    ') => error

any($sysmis, 1, $sysmis, 3) => sysmis
any(1, 1, 2, 3) => true
any(1, $true, 2, 3) => true
any(1, $false, 2, 3) => false
any(2, 1, 2, 3) => true
any(3, 1, 2, 3) => true
any(5, 1, 2, 3) => false
any(1, 1, 1, 1) => true
any($sysmis, 1, 1, 1) => sysmis
any(1, $sysmis, $sysmis, $sysmis) => sysmis
any($sysmis, $sysmis, $sysmis, $sysmis) => sysmis
any(1) => error
any('1', 2, 3, 4) => error
any(1, '2', 3, 4) => error
any(1, 2, '3', 4) => error
any(1, 2, 3, '4') => error

any('', 'a', '', 'c') => true
any('a', 'a', 'b', 'c') => true
any('b', 'a', 'b', 'c') => true
any('c', 'a', 'b', 'c') => true
any('e', 'a', 'b', 'c') => false
any('a', 'a', 'a', 'a') => true
any('', 'a', 'a', 'a') => false
any('a', '', '', '') => false
any('a') => error
any('a', 'a  ', 'b', 'c') => true
any('b   ', 'a', 'b', 'c') => true
any('c   ', 'a', 'b', 'c     ') => true
any(a, 'b', 'c', 'd') => error
any('a', b, 'c', 'd') => error
any('a', 'b', c, 'd') => error
any('a', 'b', 'c', d) => error

range(5, 1, 10) => true
range(1, 1, 10) => true
range(10, 1, 10) => true
range(-1, 1, 10) => false
range(12, 1, 10) => false
range($sysmis, 1, 10) => sysmis
range(5, 1, $sysmis) => sysmis
range(5, $sysmis, 10) => sysmis
range($sysmis, $sysmis, 10) => sysmis 
range($sysmis, 1, $sysmis) => sysmis
range($sysmis, $sysmis, $sysmis) => sysmis
range(0, 1, 8, 10, 18) => false
range(1, 1, 8, 10, 18) => true
range(6, 1, 8, 10, 18) => true
range(8, 1, 8, 10, 18) => true
range(9, 1, 8, 10, 18) => false
range(10, 1, 8, 10, 18) => true
range(13, 1, 8, 10, 18) => true
range(16, 1, 8, 10, 18) => true
range(18, 1, 8, 10, 18) => true
range(20, 1, 8, 10, 18) => false
range(1) => error
range(1, 2) => error
range(1, 2, 3, 4) => error
range(1, 2, 3, 4, 5, 6) => error
range('1', 2, 3) => error
range(1, '2', 3) => error
range(1, 2, '3') => error

range('123', '111', '888') => true
range('111', '111', '888') => true
range('888', '111', '888') => true
range('110', '111', '888') => false
range('889', '111', '888') => false
range('000', '111', '888') => false
range('999', '111', '888') => false
range('123   ', '111', '888') => true
range('123', '111   ', '888') => true
range('123', '111', '888   ') => true
range('123', '111    ', '888   ') => true
range('00', '01', '08', '10', '18') => false
range('01', '01', '08', '10', '18') => true
range('06', '01', '08', '10', '18') => true
range('08', '01', '08', '10', '18') => true
range('09', '01', '08', '10', '18') => false
range('10', '01', '08', '10', '18') => true
range('15', '01', '08', '10', '18') => true
range('18', '01', '08', '10', '18') => true
range('19', '01', '08', '10', '18') => false
range('1') => error
range('1', '2') => error
range('1', '2', '3', '4') => error
range('1', '2', '3', '4', '5', '6') => error
range(1, '2', '3') => error
range('1', 2, '3') => error
range('1', '2', 3) => error

cfvar(1, 2, 3, 4, 5) => 0.53
cfvar(1, $sysmis, 2, 3, $sysmis, 4, 5) => 0.53
cfvar(1, 2) => 0.47
cfvar(1) => error
cfvar(1, $sysmis) => sysmis
cfvar(1, 2, 3, $sysmis) => 0.50
cfvar.4(1, 2, 3, $sysmis) => sysmis
cfvar.4(1, 2, 3) => error
cfvar('x') => error
cfvar('x', 1, 2, 3) => error

max(1, 2, 3, 4, 5) => 5.00
max(1, $sysmis, 2, 3, $sysmis, 4, 5) => 5.00
max(1, 2) => 2.00
max() => error
max(1) => 1.00
max(1, $sysmis) => 1.00
max(1, 2, 3, $sysmis) => 3.00
max.4(1, 2, 3, $sysmis) => sysmis
max.4(1, 2, 3) => error

max("2", "3", "5", "1", "4") => "5"
max("1", "2") => "2"
max("1") => "1"

mean(1, 2, 3, 4, 5) => 3.00
mean(1, $sysmis, 2, 3, $sysmis, 4, 5) => 3.00
mean(1, 2) => 1.50
mean() => error
mean(1) => 1.00
mean(1, $sysmis) => 1.00
mean(1, 2, 3, $sysmis) => 2.00
mean.4(1, 2, 3, $sysmis) => sysmis
mean.4(1, 2, 3) => error

min(1, 2, 3, 4, 5) => 1.00
min(1, $sysmis, 2, 3, $sysmis, 4, 5) => 1.00
min(1, 2) => 1.00
min() => error
min(1) => 1.00
min(1, $sysmis) => 1.00
min(1, 2, 3, $sysmis) => 1.00
min.4(1, 2, 3, $sysmis) => sysmis
min.4(1, 2, 3) => error

min("2", "3", "5", "1", "4") => "1"
min("1", "2") => "1"
min("1") => "1"

sd(1, 2, 3, 4, 5) => 1.58
sd(1, $sysmis, 2, 3, $sysmis, 4, 5) => 1.58
sd(1, 2) => 0.71
sd(1) => error
sd(1, $sysmis) => sysmis
sd(1, 2, 3, $sysmis) => 1.00
sd.4(1, 2, 3, $sysmis) => sysmis
sd.4(1, 2, 3) => error
sd('x') => error
sd('x', 1, 2, 3) => error

sum(1, 2, 3, 4, 5) => 15.00
sum(1, $sysmis, 2, 3, $sysmis, 4, 5) => 15.00
sum(1, 2) => 3.00
sum() => error
sum(1) => 1.00
sum(1, $sysmis) => 1.00
sum(1, 2, 3, $sysmis) => 6.00
sum.4(1, 2, 3, $sysmis) => sysmis
sum.4(1, 2, 3) => error

variance(1, 2, 3, 4, 5) => 2.50
variance(1, $sysmis, 2, 3, $sysmis, 4, 5) => 2.50
variance(1, 2) => 0.50
variance(1) => error
variance(1, $sysmis) => sysmis
variance(1, 2, 3, $sysmis) => 1.00
variance.4(1, 2, 3, $sysmis) => sysmis
variance.4(1, 2, 3) => error
variance('x') => error
variance('x', 1, 2, 3) => error

concat('') => ""
concat('a', 'b') => "ab"
concat('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h') => "abcdefgh"
concat('abcdefgh', 'ijklmnopq') => "abcdefghijklmnopq"
concat('a', 1) => error
concat(1, 2) => error

index('abcbcde', 'bc') => 2.00
index('abcbcde', 'bcd') => 4.00
index('abcbcde', 'bcbc') => 2.00
index('abcdefgh', 'abc') => 1.00
index('abcdefgh', 'bcd') => 2.00
index('abcdefgh', 'cde') => 3.00
index('abcdefgh', 'def') => 4.00
index('abcdefgh', 'efg') => 5.00
index('abcdefgh', 'fgh') => 6.00
index('abcdefgh', 'fghi') => 0.00
index('abcdefgh', 'x') => 0.00
index('abcdefgh', 'abch') => 0.00
index('banana', 'na') => 3.00
index('banana', 'ana') => 2.00
index('', 'x') => 0.00
index('', '') => sysmis
index('abcdefgh', '') => sysmis
index('abcdefgh', 'alkjsfdjlskalkjfa') => 0.00

index('abcbcde', 'bc', 1) => 2.00
index('abcbcde', 'dc', 1) => 3.00
index('abcbcde', 'abc', 1) => 1.00
index('abcbcde', 'bc', 2) => 2.00
index('abcbcde', 'dc', 2) => 0.00
index('abcbcde', 'abc', 1) => 1.00
index('abcbcde', 'bccb', 2) => 2.00
index('abcbcde', 'bcbc', 2) => 2.00
index('abcbcde', 'bcbc', $sysmis) => sysmis

rindex('abcbcde', 'bc') => 4.00
rindex('abcbcde', 'bcd') => 4.00
rindex('abcbcde', 'bcbc') => 2.00
rindex('abcdefgh', 'abc') => 1.00
rindex('abcdefgh', 'bcd') => 2.00
rindex('abcdefgh', 'cde') => 3.00
rindex('abcdefgh', 'def') => 4.00
rindex('abcdefgh', 'efg') => 5.00
rindex('abcdefgh', 'fgh') => 6.00
rindex('abcdefgh', 'fghi') => 0.00
rindex('abcdefgh', 'x') => 0.00
rindex('abcdefgh', 'abch') => 0.00
rindex('banana', 'na') => 5.00
rindex('banana', 'ana') => 4.00
rindex('', 'x') => 0.00
rindex('', '') => sysmis
rindex('abcdefgh', '') => sysmis
rindex('abcdefgh', 'alkjsfdjlskalkjfa') => 0.00

rindex('abcbcde', 'bc', 1) => 5.00
rindex('abcbcde', 'dc', 1) => 6.00
rindex('abcbcde', 'abc', 1) => 5.00
rindex('abcbcde', 'bc', 2) => 4.00
rindex('abcbcde', 'dc', 2) => 0.00
rindex('abcbcde', 'abc', 1) => 5.00
rindex('abcbcde', 'bccb', 2) => 4.00
rindex('abcbcde', 'bcbc', 2) => 4.00
rindex('abcbcde', 'bcbc', $sysmis) => sysmis
rindex('abcbcde', 'bcbcg', 2) => sysmis
rindex('abcbcde', 'bcbcg', $sysmis) => sysmis
rindex('abcbcde', 'bcbcg', 'x') => error
rindex(1, 'bcdfkjl', 2) => error
rindex('aksj', 2, 2) => error
rindex(1, 2, 3) => error
rindex(1, 2, '3') => error

length('') => 0.00
length('a') => 1.00
length('xy') => 2.00
length('adsf    ') => 8.00
length('abcdefghijkl') => 12.00
length(0) => error
length($sysmis) => error

lower('ABCDEFGHIJKLMNOPQRSTUVWXYZ!@%&*(089') => "abcdefghijklmnopqrstuvwxyz!@%&*(089"
lower('') => ""
lower(1) => error

lpad('abc', -1) => ""
lpad('abc', 0) => "abc"
lpad('abc', 2) => "abc"
lpad('abc', 3) => "abc"
lpad('abc', 10) => "       abc"
lpad('abc', 32768) => ""
lpad('abc', $sysmis) => ""
lpad('abc', -1, '*') => ""
lpad('abc', 0, '*') => "abc"
lpad('abc', 2, '*') => "abc"
lpad('abc', 3, '*') => "abc"
lpad('abc', 10, '*') => "*******abc"
lpad('abc', 32768, '*') => ""
lpad('abc', $sysmis, '*') => ""
lpad('abc', $sysmis, '') => ""
lpad('abc', $sysmis, 'xy') => ""
lpad(0, 10) => error
lpad('abc', 'def') => error
lpad(0, 10, ' ') => error
lpad('abc', 'def', ' ') => error
lpad('x', 5, 0) => error
lpad('x', 5, 2) => error

number("123", f3.0) => 123.00
number(" 123", f3.0) => 12.00
number("123", f3.1) => 12.30
number("   ", f3.1) => sysmis
number("123", a8) => error
number("123", cca1.2) => error	# CCA is not an input format

ltrim('   abc') => "abc"
rtrim('   abc   ') => "   abc"
ltrim('abc') => "abc"
ltrim('	abc') => "	abc"
ltrim('    ') => ""
ltrim('') => ""
ltrim(8) => error
ltrim('***abc', '*') => "abc"
ltrim('abc', '*') => "abc"
ltrim('*abc', '*') => "abc"
ltrim('', '*') => ""
ltrim(8, '*') => error
ltrim(' x', 8) => error
ltrim(8, 9) => error

rpad('abc', -1) => ""
rpad('abc', 0) => "abc"
rpad('abc', 2) => "abc"
rpad('abc', 3) => "abc"
rpad('abc', 10) => "abc       "
rpad('abc', 32768) => ""
rpad('abc', $sysmis) => ""
rpad('abc', -1, '*') => ""
rpad('abc', 0, '*') => "abc"
rpad('abc', 2, '*') => "abc"
rpad('abc', 3, '*') => "abc"
rpad('abc', 10, '*') => "abc*******"
rpad('abc', 32768, '*') => ""
rpad('abc', $sysmis, '*') => ""
rpad('abc', $sysmis, '') => ""
rpad('abc', $sysmis, 'xy') => ""
rpad(0, 10) => error
rpad('abc', 'def') => error
rpad(0, 10, ' ') => error
rpad('abc', 'def', ' ') => error
rpad('x', 5, 0) => error
rpad('x', 5, 2) => error

rtrim('abc   ') => "abc"
rtrim('   abc   ') => "   abc"
rtrim('abc') => "abc"
rtrim('abc	') => "abc	"
rtrim('    ') => ""
rtrim('') => ""
rtrim(8) => error
rtrim('abc***', '*') => "abc"
rtrim('abc', '*') => "abc"
rtrim('abc*', '*') => "abc"
rtrim('', '*') => ""
rtrim(8, '*') => error
rtrim(' x', 8) => error
rtrim(8, 9) => error

string(123.56, f5.1) => "123.6"
string($sysmis, f5.1) => "   . "
string("abc", A5) => error
string(123, e1) => error	# E has a minimum width of 6 on output.
string(123, e6.0) => "1E+002"

substr('abcdefgh', -5) => ""
substr('abcdefgh', 0) => ""
substr('abcdefgh', 1) => "abcdefgh"
substr('abcdefgh', 3) => "cdefgh"
substr('abcdefgh', 5) => "efgh"
substr('abcdefgh', 6) => "fgh"
substr('abcdefgh', 7) => "gh"
substr('abcdefgh', 8) => "h"
substr('abcdefgh', 9) => ""
substr('abcdefgh', 10) => ""
substr('abcdefgh', 20) => ""
substr('abcdefgh', $sysmis) => ""
substr(0, 10) => error
substr('abcd', 'abc') => error
substr(0, 'abc') => error

substr('abcdefgh', 0, 0) => ""
substr('abcdefgh', 3, 0) => ""
substr('abcdefgh', 5, 0) => ""
substr('abcdefgh', 9, 0) => ""
substr('abcdefgh', 0, 1) => ""
substr('abcdefgh', 0, 5) => ""
substr('abcdefgh', 1, 8) => "abcdefgh"
substr('abcdefgh', 1, 10) => "abcdefgh"
substr('abcdefgh', 1, 20) => "abcdefgh"
substr('abcdefgh', 3, 4) => "cdef"
substr('abcdefgh', 5, 2) => "ef"
substr('abcdefgh', 6, 1) => "f"
substr('abcdefgh', 7, 10) => "gh"
substr('abcdefgh', 8, 1) => "h"
substr('abcdefgh', 8, 2) => "h"
substr('abcdefgh', 9, 11) => ""
substr('abcdefgh', 10, 52) => ""
substr('abcdefgh', 20, 1) => ""
substr('abcdefgh', $sysmis, 2) => ""
substr('abcdefgh', 9, $sysmis) => ""
substr('abcdefgh', $sysmis, $sysmis) => ""
substr('abc', 1, 'x') => error
substr(0, 10, 1) => error
substr(0, 10, 'x') => error
substr('abcd', 'abc', 0) => error
substr('abcd', 'abc', 'j') => error
substr(0, 'abc', 4) => error
substr(0, 'abc', 'k') => error

upcase('abcdefghijklmnopqrstuvwxyz!@%&*(089') => "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@%&*(089"
upcase('') => ""
upcase(1) => error

time.days(1) => 86400.00
time.days(-1) => -86400.00
time.days(0.5) => 43200.00
time.days('x') => error
time.days($sysmis) => sysmis

time.hms(4,50,38) => 17438.00
time.hms(12,31,35) => 45095.00
time.hms(12,47,53) => 46073.00
time.hms(1,26,0) => 5160.00
time.hms(20,58,11) => 75491.00
time.hms(7,36,5) => 27365.00
time.hms(15,43,49) => 56629.00
time.hms(4,25,9) => 15909.00
time.hms(6,49,27) => 24567.00
time.hms(2,57,52) => 10672.00
time.hms(16,45,44) => 60344.00
time.hms(21,30,57) => 77457.00
time.hms(22,30,4) => 81004.00
time.hms(1,56,51) => 7011.00
time.hms(5, 6, 7) => 18367.00
time.hms(5, 6, 0) => 18360.00
time.hms(5, 0, 7) => 18007.00
time.hms(0, 6, 7) => 367.00
time.hms(-5, 6, -7) => sysmis
time.hms(-5, 5, -7) => sysmis
time.hms($sysmis, 6, 7) => sysmis
time.hms(5, $sysmis, 7) => sysmis
time.hms(5, $sysmis, 7) => sysmis
time.hms($sysmis, $sysmis, 7) => sysmis
time.hms(5, $sysmis, $sysmis) => sysmis
time.hms($sysmis, $sysmis, 7) => sysmis
time.hms($sysmis, $sysmis, $sysmis) => sysmis

ctime.days(106272) => 1.23
ctime.hours(106272) => 29.52
ctime.minutes(106272) => 1771.20
ctime.seconds(106272) => 106272.00
ctime.days(-106272) => -1.23
ctime.hours(-106272) => -29.52
ctime.minutes(-106272) => -1771.20
ctime.seconds(-106272) => -106272.00
ctime.days($sysmis) => sysmis
ctime.hours($sysmis) => sysmis
ctime.minutes($sysmis) => sysmis
ctime.seconds($sysmis) => sysmis
ctime.days('a') => error
ctime.hours('b') => error
ctime.minutes('c') => error
ctime.seconds('d') => error

ctime.days(date.dmy(15,10,1582)) => 1.00
ctime.days(date.dmy(6,9,1719)) => 50000.00
ctime.days(date.dmy(24,1,1583)) => 102.00
ctime.days(date.dmy(14,12,1585)) => 1157.00
ctime.days(date.dmy(26,11,1621)) => 14288.00
ctime.days(date.dmy(25,12,1821)) => 87365.00
ctime.days(date.dmy(3,12,1882)) => 109623.00
ctime.days(date.dmy(6,4,2002)) => 153211.00
ctime.days(date.dmy(19,12,1999)) => 152372.00
ctime.days(date.dmy(1,10,1978)) => 144623.00
ctime.days(date.dmy(0,10,1978)) => 144622.00
ctime.days(date.dmy(32,10,1978)) => sysmis
ctime.days(date.dmy(31,0,1978)) => 144349.00
ctime.days(date.dmy(31,13,1978)) => 144745.00
ctime.days(date.dmy($sysmis,10,1978)) => sysmis
ctime.days(date.dmy(31,$sysmis,1978)) => sysmis
ctime.days(date.dmy(31,10,$sysmis)) => sysmis
ctime.days(date.dmy($sysmis,$sysmis,1978)) => sysmis
ctime.days(date.dmy(31,$sysmis,$sysmis)) => sysmis
ctime.days(date.dmy($sysmis,10,$sysmis)) => sysmis
ctime.days(date.dmy($sysmis,$sysmis,$sysmis)) => sysmis
date.dmy('a',1,2) => error
date.dmy(1,'a',2) => error
date.dmy(1,2,'a') => error
# FIXME: check out-of-range and nearly out-of-range values

yrmoda(1582,10,15) => 1.00
yrmoda(1719,9,6) => 50000.00
yrmoda(1583,1,24) => 102.00
yrmoda(1585,12,14) => 1157.00
yrmoda(1621,11,26) => 14288.00
yrmoda(1821,12,25) => 87365.00
yrmoda(1882,12,3) => 109623.00
yrmoda(2002,4,6) => 153211.00
yrmoda(1999,12,19) => 152372.00
yrmoda(1978,10,1) => 144623.00
yrmoda(1978,10,0) => 144622.00
yrmoda(1978,10,32) => sysmis
yrmoda(1978,0,31) => 144349.00
yrmoda(1978,13,31) => 144745.00
yrmoda(1978,10,$sysmis) => sysmis
yrmoda(1978,$sysmis,31) => sysmis
yrmoda($sysmis,10,31) => sysmis
yrmoda(1978,$sysmis,$sysmis) => sysmis
yrmoda($sysmis,$sysmis,31) => sysmis
yrmoda($sysmis,10,$sysmis) => sysmis
yrmoda($sysmis,$sysmis,$sysmis) => sysmis
yrmoda('a',1,2) => error
yrmoda(1,'a',2) => error
yrmoda(1,2,'a') => error
# FIXME: check out-of-range and nearly out-of-range values

ctime.days(date.mdy(6,10,1648)) + 577735 => 601716.00
ctime.days(date.mdy(6,30,1680)) + 577735 => 613424.00
ctime.days(date.mdy(7,24,1716)) + 577735 => 626596.00
ctime.days(date.mdy(6,19,1768)) + 577735 => 645554.00
ctime.days(date.mdy(8,2,1819)) + 577735 => 664224.00
ctime.days(date.mdy(3,27,1839)) + 577735 => 671401.00
ctime.days(date.mdy(4,19,1903)) + 577735 => 694799.00
ctime.days(date.mdy(8,25,1929)) + 577735 => 704424.00
ctime.days(date.mdy(9,29,1941)) + 577735 => 708842.00
ctime.days(date.mdy(4,19,1943)) + 577735 => 709409.00
ctime.days(date.mdy(10,7,1943)) + 577735 => 709580.00
ctime.days(date.mdy(3,17,1992)) + 577735 => 727274.00
ctime.days(date.mdy(2,25,1996)) + 577735 => 728714.00
ctime.days(date.mdy(11,10,2038)) + 577735 => 744313.00
ctime.days(date.mdy(7,18,2094)) + 577735 => 764652.00
# FIXME: check out-of-range and nearly out-of-range values

ctime.days(date.mdy(10,15,1582)) => 1.00
ctime.days(date.mdy(9,6,1719)) => 50000.00
ctime.days(date.mdy(1,24,1583)) => 102.00
ctime.days(date.mdy(12,14,1585)) => 1157.00
ctime.days(date.mdy(11,26,1621)) => 14288.00
ctime.days(date.mdy(12,25,1821)) => 87365.00
ctime.days(date.mdy(12,3,1882)) => 109623.00
ctime.days(date.mdy(4,6,2002)) => 153211.00
ctime.days(date.mdy(12,19,1999)) => 152372.00
ctime.days(date.mdy(10,1,1978)) => 144623.00
ctime.days(date.mdy(10,0,1978)) => 144622.00
ctime.days(date.mdy(10,32,1978)) => sysmis
ctime.days(date.mdy(0,31,1978)) => 144349.00
ctime.days(date.mdy(13,31,1978)) => 144745.00
ctime.days(date.mdy(10,$sysmis,1978)) => sysmis
ctime.days(date.mdy($sysmis,31,1978)) => sysmis
ctime.days(date.mdy(10,31,$sysmis)) => sysmis
ctime.days(date.mdy($sysmis,$sysmis,1978)) => sysmis
ctime.days(date.mdy($sysmis,31,$sysmis)) => sysmis
ctime.days(date.mdy(10,$sysmis,$sysmis)) => sysmis
ctime.days(date.mdy($sysmis,$sysmis,$sysmis)) => sysmis
date.mdy('a',1,2) => error
date.mdy(1,'a',2) => error
date.mdy(1,2,'a') => error
ctime.days(date.mdy(0,0,0)) => 152353.00
ctime.days(date.mdy(0,0,999)) => sysmis
date.mdy(1,1,1582) => sysmis
date.mdy(10,14,1582) => sysmis
date.mdy(10,15,1582) => 86400.00

ctime.days(date.moyr(1,2000)) => 152385.00
ctime.days(date.moyr(2,2000)) => 152416.00
ctime.days(date.moyr(3,2000)) => 152445.00
ctime.days(date.moyr(4,2000)) => 152476.00
ctime.days(date.moyr(5,2000)) => 152506.00
ctime.days(date.moyr(13,2000)) => 152751.00
ctime.days(date.moyr(14,2000)) => sysmis
ctime.days(date.moyr($sysmis,2000)) => sysmis
ctime.days(date.moyr(1,$sysmis)) => sysmis
ctime.days(date.moyr($sysmis,$sysmis)) => sysmis
date.moyr('a',2000) => error
date.moyr(5,'a') => error
date.moyr('a','b') => error

ctime.days(date.qyr(1,2000)) => 152385.00
ctime.days(date.qyr(2,2000)) => 152476.00
ctime.days(date.qyr(5,2000)) => 152751.00
ctime.days(date.qyr(6,2000)) => sysmis
ctime.days(date.qyr($sysmis,2000)) => sysmis
ctime.days(date.qyr(1,$sysmis)) => sysmis
ctime.days(date.qyr($sysmis,$sysmis)) => sysmis
date.qyr('a',2000) => error
date.qyr(5,'a') => error
date.qyr('a','b') => error

ctime.days(date.wkyr(1,2000)) => 152385.00
ctime.days(date.wkyr(15,1999)) => 152118.00
ctime.days(date.wkyr(36,1999)) => 152265.00
ctime.days(date.wkyr(54,1999)) => sysmis
ctime.days(date.wkyr($sysmis,1999)) => sysmis
ctime.days(date.wkyr(1,$sysmis)) => sysmis
ctime.days(date.wkyr($sysmis,$sysmis)) => sysmis
date.wkyr('a',1999) => error
date.wkyr(5,'a') => error
date.wkyr('a','b') => error

ctime.days(date.yrday(2000,1)) => 152385.00
ctime.days(date.yrday(2000,100)) => 152484.00
ctime.days(date.yrday(2000,253)) => 152637.00
ctime.days(date.yrday(2000,500)) => sysmis
ctime.days(date.yrday(2000,-100)) => sysmis
ctime.days(date.yrday(1999,$sysmis)) => sysmis
ctime.days(date.yrday($sysmis,1)) => sysmis
ctime.days(date.yrday($sysmis,$sysmis)) => sysmis
date.yrday(1999,'a') => error
date.yrday('a',5) => error
date.yrday('a','b') => error

xdate.date(date.mdy(6,10,1648) + time.hms(0,0,0)) / 86400 => 23981.00
xdate.date(date.mdy(6,30,1680) + time.hms(4,50,38)) / 86400 => 35689.00
xdate.date(date.mdy(7,24,1716) + time.hms(12,31,35)) / 86400 => 48861.00
xdate.date(date.mdy(6,19,1768) + time.hms(12,47,53)) / 86400 => 67819.00
xdate.date(date.mdy(8,2,1819) + time.hms(1,26,0)) / 86400 => 86489.00
xdate.date(date.mdy(3,27,1839) + time.hms(20,58,11)) / 86400 => 93666.00
xdate.date(date.mdy(4,19,1903) + time.hms(7,36,5)) / 86400 => 117064.00
xdate.date(date.mdy(8,25,1929) + time.hms(15,43,49)) / 86400 => 126689.00
xdate.date(date.mdy(9,29,1941) + time.hms(4,25,9)) / 86400 => 131107.00
xdate.date(date.mdy(4,19,1943) + time.hms(6,49,27)) / 86400 => 131674.00
xdate.date(date.mdy(10,7,1943) + time.hms(2,57,52)) / 86400 => 131845.00
xdate.date(date.mdy(3,17,1992) + time.hms(16,45,44)) / 86400 => 149539.00
xdate.date(date.mdy(2,25,1996) + time.hms(21,30,57)) / 86400 => 150979.00
xdate.date(date.mdy(9,29,41) + time.hms(4,25,9)) / 86400 => 131107.00
xdate.date(date.mdy(4,19,43) + time.hms(6,49,27)) / 86400 => 131674.00
xdate.date(date.mdy(10,7,43) + time.hms(2,57,52)) / 86400 => 131845.00
xdate.date(date.mdy(3,17,92) + time.hms(16,45,44)) / 86400 => 149539.00
xdate.date(date.mdy(2,25,96) + time.hms(21,30,57)) / 86400 => 150979.00
xdate.date(date.mdy(11,10,2038) + time.hms(22,30,4)) / 86400 => 166578.00
xdate.date(date.mdy(7,18,2094) + time.hms(1,56,51)) / 86400 => 186917.00
xdate.date(123.4) => 0.00
xdate.date('') => error

xdate.hour(date.mdy(6,10,1648) + time.hms(0,0,0)) => 0.00
xdate.hour(date.mdy(6,30,1680) + time.hms(4,50,38)) => 4.00
xdate.hour(date.mdy(7,24,1716) + time.hms(12,31,35)) => 12.00
xdate.hour(date.mdy(6,19,1768) + time.hms(12,47,53)) => 12.00
xdate.hour(date.mdy(8,2,1819) + time.hms(1,26,0)) => 1.00
xdate.hour(date.mdy(3,27,1839) + time.hms(20,58,11)) => 20.00
xdate.hour(date.mdy(4,19,1903) + time.hms(7,36,5)) => 7.00
xdate.hour(date.mdy(8,25,1929) + time.hms(15,43,49)) => 15.00
xdate.hour(date.mdy(9,29,1941) + time.hms(4,25,9)) => 4.00
xdate.hour(date.mdy(4,19,1943) + time.hms(6,49,27)) => 6.00
xdate.hour(date.mdy(10,7,1943) + time.hms(2,57,52)) => 2.00
xdate.hour(date.mdy(3,17,1992) + time.hms(16,45,44)) => 16.00
xdate.hour(date.mdy(2,25,1996) + time.hms(21,30,57)) => 21.00
xdate.hour(date.mdy(9,29,41) + time.hms(4,25,9)) => 4.00
xdate.hour(date.mdy(4,19,43) + time.hms(6,49,27)) => 6.00
xdate.hour(date.mdy(10,7,43) + time.hms(2,57,52)) => 2.00
xdate.hour(date.mdy(3,17,92) + time.hms(16,45,44)) => 16.00
xdate.hour(date.mdy(2,25,96) + time.hms(21,30,57)) => 21.00
xdate.hour(date.mdy(11,10,2038) + time.hms(22,30,4)) => 22.00
xdate.hour(date.mdy(7,18,2094) + time.hms(1,56,51)) => 1.00
xdate.hour(-1) => -1.00
xdate.hour(1) => 0.00
xdate.hour($sysmis) => sysmis
xdate.hour('') => error

xdate.jday(date.mdy(6,10,1648) + time.hms(0,0,0)) => 162.00
xdate.jday(date.mdy(6,30,1680) + time.hms(4,50,38)) => 182.00
xdate.jday(date.mdy(7,24,1716) + time.hms(12,31,35)) => 206.00
xdate.jday(date.mdy(6,19,1768) + time.hms(12,47,53)) => 171.00
xdate.jday(date.mdy(8,2,1819) + time.hms(1,26,0)) => 214.00
xdate.jday(date.mdy(3,27,1839) + time.hms(20,58,11)) => 86.00
xdate.jday(date.mdy(4,19,1903) + time.hms(7,36,5)) => 109.00
xdate.jday(date.mdy(8,25,1929) + time.hms(15,43,49)) => 237.00
xdate.jday(date.mdy(9,29,1941) + time.hms(4,25,9)) => 272.00
xdate.jday(date.mdy(4,19,1943) + time.hms(6,49,27)) => 109.00
xdate.jday(date.mdy(10,7,1943) + time.hms(2,57,52)) => 280.00
xdate.jday(date.mdy(3,17,1992) + time.hms(16,45,44)) => 77.00
xdate.jday(date.mdy(2,25,1996) + time.hms(21,30,57)) => 56.00
xdate.jday(date.mdy(9,29,41) + time.hms(4,25,9)) => 272.00
xdate.jday(date.mdy(4,19,43) + time.hms(6,49,27)) => 109.00
xdate.jday(date.mdy(10,7,43) + time.hms(2,57,52)) => 280.00
xdate.jday(date.mdy(3,17,92) + time.hms(16,45,44)) => 77.00
xdate.jday(date.mdy(2,25,96) + time.hms(21,30,57)) => 56.00
xdate.jday(date.mdy(11,10,2038) + time.hms(22,30,4)) => 314.00
xdate.jday(date.mdy(7,18,2094) + time.hms(1,56,51)) => 199.00
xdate.jday(0) => sysmis
xdate.jday(1) => sysmis
xdate.jday(86400) => 288.00

xdate.mday(date.mdy(6,10,1648) + time.hms(0,0,0)) => 10.00
xdate.mday(date.mdy(6,30,1680) + time.hms(4,50,38)) => 30.00
xdate.mday(date.mdy(7,24,1716) + time.hms(12,31,35)) => 24.00
xdate.mday(date.mdy(6,19,1768) + time.hms(12,47,53)) => 19.00
xdate.mday(date.mdy(8,2,1819) + time.hms(1,26,0)) => 2.00
xdate.mday(date.mdy(3,27,1839) + time.hms(20,58,11)) => 27.00
xdate.mday(date.mdy(4,19,1903) + time.hms(7,36,5)) => 19.00
xdate.mday(date.mdy(8,25,1929) + time.hms(15,43,49)) => 25.00
xdate.mday(date.mdy(9,29,1941) + time.hms(4,25,9)) => 29.00
xdate.mday(date.mdy(4,19,1943) + time.hms(6,49,27)) => 19.00
xdate.mday(date.mdy(10,7,1943) + time.hms(2,57,52)) => 7.00
xdate.mday(date.mdy(3,17,1992) + time.hms(16,45,44)) => 17.00
xdate.mday(date.mdy(2,25,1996) + time.hms(21,30,57)) => 25.00
xdate.mday(date.mdy(9,29,41) + time.hms(4,25,9)) => 29.00
xdate.mday(date.mdy(4,19,43) + time.hms(6,49,27)) => 19.00
xdate.mday(date.mdy(10,7,43) + time.hms(2,57,52)) => 7.00
xdate.mday(date.mdy(3,17,92) + time.hms(16,45,44)) => 17.00
xdate.mday(date.mdy(2,25,96) + time.hms(21,30,57)) => 25.00
xdate.mday(date.mdy(11,10,2038) + time.hms(22,30,4)) => 10.00
xdate.mday(date.mdy(7,18,2094) + time.hms(1,56,51)) => 18.00

xdate.minute(date.mdy(6,10,1648) + time.hms(0,0,0)) => 0.00
xdate.minute(date.mdy(6,30,1680) + time.hms(4,50,38)) => 50.00
xdate.minute(date.mdy(7,24,1716) + time.hms(12,31,35)) => 31.00
xdate.minute(date.mdy(6,19,1768) + time.hms(12,47,53)) => 47.00
xdate.minute(date.mdy(8,2,1819) + time.hms(1,26,0)) => 26.00
xdate.minute(date.mdy(3,27,1839) + time.hms(20,58,11)) => 58.00
xdate.minute(date.mdy(4,19,1903) + time.hms(7,36,5)) => 36.00
xdate.minute(date.mdy(8,25,1929) + time.hms(15,43,49)) => 43.00
xdate.minute(date.mdy(9,29,1941) + time.hms(4,25,9)) => 25.00
xdate.minute(date.mdy(4,19,1943) + time.hms(6,49,27)) => 49.00
xdate.minute(date.mdy(10,7,1943) + time.hms(2,57,52)) => 57.00
xdate.minute(date.mdy(3,17,1992) + time.hms(16,45,44)) => 45.00
xdate.minute(date.mdy(2,25,1996) + time.hms(21,30,57)) => 30.00
xdate.minute(date.mdy(9,29,41) + time.hms(4,25,9)) => 25.00
xdate.minute(date.mdy(4,19,43) + time.hms(6,49,27)) => 49.00
xdate.minute(date.mdy(10,7,43) + time.hms(2,57,52)) => 57.00
xdate.minute(date.mdy(3,17,92) + time.hms(16,45,44)) => 45.00
xdate.minute(date.mdy(2,25,96) + time.hms(21,30,57)) => 30.00
xdate.minute(date.mdy(11,10,2038) + time.hms(22,30,4)) => 30.00
xdate.minute(date.mdy(7,18,2094) + time.hms(1,56,51)) => 56.00

xdate.month(date.mdy(6,10,1648) + time.hms(0,0,0)) => 6.00
xdate.month(date.mdy(6,30,1680) + time.hms(4,50,38)) => 6.00
xdate.month(date.mdy(7,24,1716) + time.hms(12,31,35)) => 7.00
xdate.month(date.mdy(6,19,1768) + time.hms(12,47,53)) => 6.00
xdate.month(date.mdy(8,2,1819) + time.hms(1,26,0)) => 8.00
xdate.month(date.mdy(3,27,1839) + time.hms(20,58,11)) => 3.00
xdate.month(date.mdy(4,19,1903) + time.hms(7,36,5)) => 4.00
xdate.month(date.mdy(8,25,1929) + time.hms(15,43,49)) => 8.00
xdate.month(date.mdy(9,29,1941) + time.hms(4,25,9)) => 9.00
xdate.month(date.mdy(4,19,1943) + time.hms(6,49,27)) => 4.00
xdate.month(date.mdy(10,7,1943) + time.hms(2,57,52)) => 10.00
xdate.month(date.mdy(3,17,1992) + time.hms(16,45,44)) => 3.00
xdate.month(date.mdy(2,25,1996) + time.hms(21,30,57)) => 2.00
xdate.month(date.mdy(9,29,41) + time.hms(4,25,9)) => 9.00
xdate.month(date.mdy(4,19,43) + time.hms(6,49,27)) => 4.00
xdate.month(date.mdy(10,7,43) + time.hms(2,57,52)) => 10.00
xdate.month(date.mdy(3,17,92) + time.hms(16,45,44)) => 3.00
xdate.month(date.mdy(2,25,96) + time.hms(21,30,57)) => 2.00
xdate.month(date.mdy(11,10,2038) + time.hms(22,30,4)) => 11.00
xdate.month(date.mdy(7,18,2094) + time.hms(1,56,51)) => 7.00

xdate.quarter(date.mdy(6,10,1648) + time.hms(0,0,0)) => 2.00
xdate.quarter(date.mdy(6,30,1680) + time.hms(4,50,38)) => 2.00
xdate.quarter(date.mdy(7,24,1716) + time.hms(12,31,35)) => 3.00
xdate.quarter(date.mdy(6,19,1768) + time.hms(12,47,53)) => 2.00
xdate.quarter(date.mdy(8,2,1819) + time.hms(1,26,0)) => 3.00
xdate.quarter(date.mdy(3,27,1839) + time.hms(20,58,11)) => 1.00
xdate.quarter(date.mdy(4,19,1903) + time.hms(7,36,5)) => 2.00
xdate.quarter(date.mdy(8,25,1929) + time.hms(15,43,49)) => 3.00
xdate.quarter(date.mdy(9,29,1941) + time.hms(4,25,9)) => 3.00
xdate.quarter(date.mdy(4,19,1943) + time.hms(6,49,27)) => 2.00
xdate.quarter(date.mdy(10,7,1943) + time.hms(2,57,52)) => 4.00
xdate.quarter(date.mdy(3,17,1992) + time.hms(16,45,44)) => 1.00
xdate.quarter(date.mdy(2,25,1996) + time.hms(21,30,57)) => 1.00
xdate.quarter(date.mdy(9,29,41) + time.hms(4,25,9)) => 3.00
xdate.quarter(date.mdy(4,19,43) + time.hms(6,49,27)) => 2.00
xdate.quarter(date.mdy(10,7,43) + time.hms(2,57,52)) => 4.00
xdate.quarter(date.mdy(3,17,92) + time.hms(16,45,44)) => 1.00
xdate.quarter(date.mdy(2,25,96) + time.hms(21,30,57)) => 1.00
xdate.quarter(date.mdy(11,10,2038) + time.hms(22,30,4)) => 4.00
xdate.quarter(date.mdy(7,18,2094) + time.hms(1,56,51)) => 3.00

xdate.second(date.mdy(6,10,1648) + time.hms(0,0,0)) => 0.00
xdate.second(date.mdy(6,30,1680) + time.hms(4,50,38)) => 38.00
xdate.second(date.mdy(7,24,1716) + time.hms(12,31,35)) => 35.00
xdate.second(date.mdy(6,19,1768) + time.hms(12,47,53)) => 53.00
xdate.second(date.mdy(8,2,1819) + time.hms(1,26,0)) => 0.00
xdate.second(date.mdy(3,27,1839) + time.hms(20,58,11)) => 11.00
xdate.second(date.mdy(4,19,1903) + time.hms(7,36,5)) => 5.00
xdate.second(date.mdy(8,25,1929) + time.hms(15,43,49)) => 49.00
xdate.second(date.mdy(9,29,1941) + time.hms(4,25,9)) => 9.00
xdate.second(date.mdy(4,19,1943) + time.hms(6,49,27)) => 27.00
xdate.second(date.mdy(10,7,1943) + time.hms(2,57,52)) => 52.00
xdate.second(date.mdy(3,17,1992) + time.hms(16,45,44)) => 44.00
xdate.second(date.mdy(2,25,1996) + time.hms(21,30,57)) => 57.00
xdate.second(date.mdy(9,29,41) + time.hms(4,25,9)) => 9.00
xdate.second(date.mdy(4,19,43) + time.hms(6,49,27)) => 27.00
xdate.second(date.mdy(10,7,43) + time.hms(2,57,52)) => 52.00
xdate.second(date.mdy(3,17,92) + time.hms(16,45,44)) => 44.00
xdate.second(date.mdy(2,25,96) + time.hms(21,30,57)) => 57.00
xdate.second(date.mdy(11,10,2038) + time.hms(22,30,4)) => 4.00
xdate.second(date.mdy(7,18,2094) + time.hms(1,56,51)) => 51.00

xdate.tday(date.mdy(6,10,1648) + time.hms(0,0,0)) => 23981.00
xdate.tday(date.mdy(6,30,1680) + time.hms(4,50,38)) => 35689.00
xdate.tday(date.mdy(7,24,1716) + time.hms(12,31,35)) => 48861.00
xdate.tday(date.mdy(6,19,1768) + time.hms(12,47,53)) => 67819.00
xdate.tday(date.mdy(8,2,1819) + time.hms(1,26,0)) => 86489.00
xdate.tday(date.mdy(3,27,1839) + time.hms(20,58,11)) => 93666.00
xdate.tday(date.mdy(4,19,1903) + time.hms(7,36,5)) => 117064.00
xdate.tday(date.mdy(8,25,1929) + time.hms(15,43,49)) => 126689.00
xdate.tday(date.mdy(9,29,1941) + time.hms(4,25,9)) => 131107.00
xdate.tday(date.mdy(4,19,1943) + time.hms(6,49,27)) => 131674.00
xdate.tday(date.mdy(10,7,1943) + time.hms(2,57,52)) => 131845.00
xdate.tday(date.mdy(3,17,1992) + time.hms(16,45,44)) => 149539.00
xdate.tday(date.mdy(2,25,1996) + time.hms(21,30,57)) => 150979.00
xdate.tday(date.mdy(9,29,41) + time.hms(4,25,9)) => 131107.00
xdate.tday(date.mdy(4,19,43) + time.hms(6,49,27)) => 131674.00
xdate.tday(date.mdy(10,7,43) + time.hms(2,57,52)) => 131845.00
xdate.tday(date.mdy(3,17,92) + time.hms(16,45,44)) => 149539.00
xdate.tday(date.mdy(2,25,96) + time.hms(21,30,57)) => 150979.00
xdate.tday(date.mdy(11,10,2038) + time.hms(22,30,4)) => 166578.00
xdate.tday(date.mdy(7,18,2094) + time.hms(1,56,51)) => 186917.00

xdate.time(date.mdy(6,10,1648) + time.hms(0,0,0)) => 0.00
xdate.time(date.mdy(6,30,1680) + time.hms(4,50,38)) => 17438.00
xdate.time(date.mdy(7,24,1716) + time.hms(12,31,35)) => 45095.00
xdate.time(date.mdy(6,19,1768) + time.hms(12,47,53)) => 46073.00
xdate.time(date.mdy(8,2,1819) + time.hms(1,26,0)) => 5160.00
xdate.time(date.mdy(3,27,1839) + time.hms(20,58,11)) => 75491.00
xdate.time(date.mdy(4,19,1903) + time.hms(7,36,5)) => 27365.00
xdate.time(date.mdy(8,25,1929) + time.hms(15,43,49)) => 56629.00
xdate.time(date.mdy(9,29,1941) + time.hms(4,25,9)) => 15909.00
xdate.time(date.mdy(4,19,1943) + time.hms(6,49,27)) => 24567.00
xdate.time(date.mdy(10,7,1943) + time.hms(2,57,52)) => 10672.00
xdate.time(date.mdy(3,17,1992) + time.hms(16,45,44)) => 60344.00
xdate.time(date.mdy(2,25,1996) + time.hms(21,30,57)) => 77457.00
xdate.time(date.mdy(9,29,41) + time.hms(4,25,9)) => 15909.00
xdate.time(date.mdy(4,19,43) + time.hms(6,49,27)) => 24567.00
xdate.time(date.mdy(10,7,43) + time.hms(2,57,52)) => 10672.00
xdate.time(date.mdy(3,17,92) + time.hms(16,45,44)) => 60344.00
xdate.time(date.mdy(2,25,96) + time.hms(21,30,57)) => 77457.00
xdate.time(date.mdy(11,10,2038) + time.hms(22,30,4)) => 81004.00
xdate.time(date.mdy(7,18,2094) + time.hms(1,56,51)) => 7011.00

xdate.week(date.mdy(6,10,1648) + time.hms(0,0,0)) => 24.00
xdate.week(date.mdy(6,30,1680) + time.hms(4,50,38)) => 26.00
xdate.week(date.mdy(7,24,1716) + time.hms(12,31,35)) => 30.00
xdate.week(date.mdy(6,19,1768) + time.hms(12,47,53)) => 25.00
xdate.week(date.mdy(8,2,1819) + time.hms(1,26,0)) => 31.00
xdate.week(date.mdy(3,27,1839) + time.hms(20,58,11)) => 13.00
xdate.week(date.mdy(4,19,1903) + time.hms(7,36,5)) => 16.00
xdate.week(date.mdy(8,25,1929) + time.hms(15,43,49)) => 34.00
xdate.week(date.mdy(9,29,1941) + time.hms(4,25,9)) => 39.00
xdate.week(date.mdy(4,19,1943) + time.hms(6,49,27)) => 16.00
xdate.week(date.mdy(10,7,1943) + time.hms(2,57,52)) => 40.00
xdate.week(date.mdy(3,17,1992) + time.hms(16,45,44)) => 11.00
xdate.week(date.mdy(2,25,1996) + time.hms(21,30,57)) => 8.00
xdate.week(date.mdy(9,29,41) + time.hms(4,25,9)) => 39.00
xdate.week(date.mdy(4,19,43) + time.hms(6,49,27)) => 16.00
xdate.week(date.mdy(10,7,43) + time.hms(2,57,52)) => 40.00
xdate.week(date.mdy(3,17,92) + time.hms(16,45,44)) => 11.00
xdate.week(date.mdy(2,25,96) + time.hms(21,30,57)) => 8.00
xdate.week(date.mdy(11,10,2038) + time.hms(22,30,4)) => 45.00
xdate.week(date.mdy(7,18,2094) + time.hms(1,56,51)) => 29.00

xdate.wkday(date.mdy(6,10,1648)) => 4.00
xdate.wkday(date.mdy(6,30,1680)) => 1.00
xdate.wkday(date.mdy(7,24,1716)) => 6.00
xdate.wkday(date.mdy(6,19,1768)) => 1.00
xdate.wkday(date.mdy(8,2,1819)) => 2.00
xdate.wkday(date.mdy(3,27,1839)) => 4.00
xdate.wkday(date.mdy(4,19,1903)) => 1.00
xdate.wkday(date.mdy(8,25,1929)) => 1.00
xdate.wkday(date.mdy(9,29,1941)) => 2.00
xdate.wkday(date.mdy(4,19,1943)) => 2.00
xdate.wkday(date.mdy(10,7,1943)) => 5.00
xdate.wkday(date.mdy(3,17,1992)) => 3.00
xdate.wkday(date.mdy(2,25,1996)) => 1.00
xdate.wkday(date.mdy(9,29,41)) => 2.00
xdate.wkday(date.mdy(4,19,43)) => 2.00
xdate.wkday(date.mdy(10,7,43)) => 5.00
xdate.wkday(date.mdy(3,17,92)) => 3.00
xdate.wkday(date.mdy(2,25,96)) => 1.00
xdate.wkday(date.mdy(11,10,2038)) => 4.00
xdate.wkday(date.mdy(7,18,2094)) => 1.00

xdate.year(date.mdy(6,10,1648) + time.hms(0,0,0)) => 1648.00
xdate.year(date.mdy(6,30,1680) + time.hms(4,50,38)) => 1680.00
xdate.year(date.mdy(7,24,1716) + time.hms(12,31,35)) => 1716.00
xdate.year(date.mdy(6,19,1768) + time.hms(12,47,53)) => 1768.00
xdate.year(date.mdy(8,2,1819) + time.hms(1,26,0)) => 1819.00
xdate.year(date.mdy(3,27,1839) + time.hms(20,58,11)) => 1839.00
xdate.year(date.mdy(4,19,1903) + time.hms(7,36,5)) => 1903.00
xdate.year(date.mdy(8,25,1929) + time.hms(15,43,49)) => 1929.00
xdate.year(date.mdy(9,29,1941) + time.hms(4,25,9)) => 1941.00
xdate.year(date.mdy(4,19,1943) + time.hms(6,49,27)) => 1943.00
xdate.year(date.mdy(10,7,1943) + time.hms(2,57,52)) => 1943.00
xdate.year(date.mdy(3,17,1992) + time.hms(16,45,44)) => 1992.00
xdate.year(date.mdy(2,25,1996) + time.hms(21,30,57)) => 1996.00
xdate.year(date.mdy(9,29,41) + time.hms(4,25,9)) => 1941.00
xdate.year(date.mdy(4,19,43) + time.hms(6,49,27)) => 1943.00
xdate.year(date.mdy(10,7,43) + time.hms(2,57,52)) => 1943.00
xdate.year(date.mdy(3,17,92) + time.hms(16,45,44)) => 1992.00
xdate.year(date.mdy(2,25,96) + time.hms(21,30,57)) => 1996.00
xdate.year(date.mdy(11,10,2038) + time.hms(22,30,4)) => 2038.00
xdate.year(date.mdy(7,18,2094) + time.hms(1,56,51)) => 2094.00

datediff(date.mdy(6,10,1648), date.mdy(6,30,1680), 'years') => -32.00
datediff(date.mdy(6,30,1680), date.mdy(7,24,1716), 'years') => -36.00
datediff(date.mdy(7,24,1716), date.mdy(6,19,1768), 'years') => -51.00
datediff(date.mdy(6,19,1768), date.mdy(8,2,1819), 'years') => -51.00
datediff(date.mdy(8,2,1819), date.mdy(3,27,1839), 'years') => -19.00
datediff(date.mdy(3,27,1839), date.mdy(4,19,1903), 'years') => -64.00
datediff(date.mdy(4,19,1903), date.mdy(8,25,1929), 'years') => -26.00
datediff(date.mdy(8,25,1929), date.mdy(9,29,1941), 'years') => -12.00
datediff(date.mdy(9,29,1941), date.mdy(4,19,1943), 'years') => -1.00
datediff(date.mdy(4,19,1943), date.mdy(10,7,1943), 'years') => 0.00
datediff(date.mdy(10,7,1943), date.mdy(3,17,1992), 'years') => -48.00
datediff(date.mdy(3,17,1992), date.mdy(2,25,1996), 'years') => -3.00
datediff(date.mdy(9,29,41), date.mdy(2,25,1996), 'years') => -54.00
datediff(date.mdy(9,29,41), date.mdy(4,19,43), 'years') => -1.00
datediff(date.mdy(4,19,43), date.mdy(10,7,43), 'years') => 0.00
datediff(date.mdy(10,7,43), date.mdy(3,17,92), 'years') => -48.00
datediff(date.mdy(3,17,92), date.mdy(2,25,96), 'years') => -3.00
datediff(date.mdy(2,25,96), date.mdy(11,10,2038), 'years') => -42.00
datediff(date.mdy(11,10,2038), date.mdy(7,18,2094), 'years') => -55.00
datediff(date.mdy(2,29,1900), date.mdy(2,29,1904), 'years') => -3.00
datediff(date.mdy(2,29,1904), date.mdy(2,29,1908), 'years') => -4.00
datediff(date.mdy(2,29,1900), date.mdy(2,28,1903), 'years') => -2.00

datediff(date.mdy(6,10,1648), date.mdy(6,30,1680), 'quarters') => -128.00
datediff(date.mdy(6,30,1680), date.mdy(7,24,1716), 'quarters') => -144.00
datediff(date.mdy(7,24,1716), date.mdy(6,19,1768), 'quarters') => -207.00
datediff(date.mdy(6,19,1768), date.mdy(8,2,1819), 'quarters') => -204.00
datediff(date.mdy(8,2,1819), date.mdy(3,27,1839), 'quarters') => -78.00
datediff(date.mdy(3,27,1839), date.mdy(4,19,1903), 'quarters') => -256.00
datediff(date.mdy(4,19,1903), date.mdy(8,25,1929), 'quarters') => -105.00
datediff(date.mdy(8,25,1929), date.mdy(9,29,1941), 'quarters') => -48.00
datediff(date.mdy(9,29,1941), date.mdy(4,19,1943), 'quarters') => -6.00
datediff(date.mdy(4,19,1943), date.mdy(10,7,1943), 'quarters') => -1.00
datediff(date.mdy(10,7,1943), date.mdy(3,17,1992), 'quarters') => -193.00
datediff(date.mdy(3,17,1992), date.mdy(2,25,1996), 'quarters') => -15.00
datediff(date.mdy(9,29,41), date.mdy(2,25,1996), 'quarters') => -217.00
datediff(date.mdy(9,29,41), date.mdy(4,19,43), 'quarters') => -6.00
datediff(date.mdy(4,19,43), date.mdy(10,7,43), 'quarters') => -1.00
datediff(date.mdy(10,7,43), date.mdy(3,17,92), 'quarters') => -193.00
datediff(date.mdy(3,17,92), date.mdy(2,25,96), 'quarters') => -15.00
datediff(date.mdy(2,25,96), date.mdy(11,10,2038), 'quarters') => -170.00
datediff(date.mdy(11,10,2038), date.mdy(7,18,2094), 'quarters') => -222.00
datediff(date.mdy(2,29,1900), date.mdy(2,29,1904), 'quarters') => -15.00
datediff(date.mdy(2,29,1904), date.mdy(2,29,1908), 'quarters') => -16.00
datediff(date.mdy(2,29,1900), date.mdy(2,28,1903), 'quarters') => -11.00

datediff(date.mdy(6,10,1648), date.mdy(6,30,1680), 'months') => -384.00
datediff(date.mdy(6,30,1680), date.mdy(7,24,1716), 'months') => -432.00
datediff(date.mdy(7,24,1716), date.mdy(6,19,1768), 'months') => -622.00
datediff(date.mdy(6,19,1768), date.mdy(8,2,1819), 'months') => -613.00
datediff(date.mdy(8,2,1819), date.mdy(3,27,1839), 'months') => -235.00
datediff(date.mdy(3,27,1839), date.mdy(4,19,1903), 'months') => -768.00
datediff(date.mdy(4,19,1903), date.mdy(8,25,1929), 'months') => -316.00
datediff(date.mdy(8,25,1929), date.mdy(9,29,1941), 'months') => -145.00
datediff(date.mdy(9,29,1941), date.mdy(4,19,1943), 'months') => -18.00
datediff(date.mdy(4,19,1943), date.mdy(10,7,1943), 'months') => -5.00
datediff(date.mdy(10,7,1943), date.mdy(3,17,1992), 'months') => -581.00
datediff(date.mdy(3,17,1992), date.mdy(2,25,1996), 'months') => -47.00
datediff(date.mdy(9,29,41), date.mdy(2,25,1996), 'months') => -652.00
datediff(date.mdy(9,29,41), date.mdy(4,19,43), 'months') => -18.00
datediff(date.mdy(4,19,43), date.mdy(10,7,43), 'months') => -5.00
datediff(date.mdy(10,7,43), date.mdy(3,17,92), 'months') => -581.00
datediff(date.mdy(3,17,92), date.mdy(2,25,96), 'months') => -47.00
datediff(date.mdy(2,25,96), date.mdy(11,10,2038), 'months') => -512.00
datediff(date.mdy(11,10,2038), date.mdy(7,18,2094), 'months') => -668.00
datediff(date.mdy(2,29,1900), date.mdy(2,29,1904), 'months') => -47.00
datediff(date.mdy(2,29,1904), date.mdy(2,29,1908), 'months') => -48.00
datediff(date.mdy(2,29,1900), date.mdy(2,28,1903), 'months') => -35.00

datediff(date.mdy(6,10,1648), date.mdy(6,30,1680), 'weeks') => -1672.00
datediff(date.mdy(6,30,1680), date.mdy(7,24,1716), 'weeks') => -1881.00
datediff(date.mdy(7,24,1716), date.mdy(6,19,1768), 'weeks') => -2708.00
datediff(date.mdy(6,19,1768), date.mdy(8,2,1819), 'weeks') => -2667.00
datediff(date.mdy(8,2,1819), date.mdy(3,27,1839), 'weeks') => -1025.00
datediff(date.mdy(3,27,1839), date.mdy(4,19,1903), 'weeks') => -3342.00
datediff(date.mdy(4,19,1903), date.mdy(8,25,1929), 'weeks') => -1375.00
datediff(date.mdy(8,25,1929), date.mdy(9,29,1941), 'weeks') => -631.00
datediff(date.mdy(9,29,1941), date.mdy(4,19,1943), 'weeks') => -81.00
datediff(date.mdy(4,19,1943), date.mdy(10,7,1943), 'weeks') => -24.00
datediff(date.mdy(10,7,1943), date.mdy(3,17,1992), 'weeks') => -2527.00
datediff(date.mdy(3,17,1992), date.mdy(2,25,1996), 'weeks') => -205.00
datediff(date.mdy(9,29,41), date.mdy(2,25,1996), 'weeks') => -2838.00
datediff(date.mdy(9,29,41), date.mdy(4,19,43), 'weeks') => -81.00
datediff(date.mdy(4,19,43), date.mdy(10,7,43), 'weeks') => -24.00
datediff(date.mdy(10,7,43), date.mdy(3,17,92), 'weeks') => -2527.00
datediff(date.mdy(3,17,92), date.mdy(2,25,96), 'weeks') => -205.00
datediff(date.mdy(2,25,96), date.mdy(11,10,2038), 'weeks') => -2228.00
datediff(date.mdy(11,10,2038), date.mdy(7,18,2094), 'weeks') => -2905.00
datediff(date.mdy(2,29,1900), date.mdy(2,29,1904), 'weeks') => -208.00
datediff(date.mdy(2,29,1904), date.mdy(2,29,1908), 'weeks') => -208.00
datediff(date.mdy(2,29,1900), date.mdy(2,28,1903), 'weeks') => -156.00

datediff(date.mdy(6,10,1648), date.mdy(6,30,1680), 'days') => -11708.00
datediff(date.mdy(6,30,1680), date.mdy(7,24,1716), 'days') => -13172.00
datediff(date.mdy(7,24,1716), date.mdy(6,19,1768), 'days') => -18958.00
datediff(date.mdy(6,19,1768), date.mdy(8,2,1819), 'days') => -18670.00
datediff(date.mdy(8,2,1819), date.mdy(3,27,1839), 'days') => -7177.00
datediff(date.mdy(3,27,1839), date.mdy(4,19,1903), 'days') => -23398.00
datediff(date.mdy(4,19,1903), date.mdy(8,25,1929), 'days') => -9625.00
datediff(date.mdy(8,25,1929), date.mdy(9,29,1941), 'days') => -4418.00
datediff(date.mdy(9,29,1941), date.mdy(4,19,1943), 'days') => -567.00
datediff(date.mdy(4,19,1943), date.mdy(10,7,1943), 'days') => -171.00
datediff(date.mdy(10,7,1943), date.mdy(3,17,1992), 'days') => -17694.00
datediff(date.mdy(3,17,1992), date.mdy(2,25,1996), 'days') => -1440.00
datediff(date.mdy(9,29,41), date.mdy(2,25,1996), 'days') => -19872.00
datediff(date.mdy(9,29,41), date.mdy(4,19,43), 'days') => -567.00
datediff(date.mdy(4,19,43), date.mdy(10,7,43), 'days') => -171.00
datediff(date.mdy(10,7,43), date.mdy(3,17,92), 'days') => -17694.00
datediff(date.mdy(3,17,92), date.mdy(2,25,96), 'days') => -1440.00
datediff(date.mdy(2,25,96), date.mdy(11,10,2038), 'days') => -15599.00
datediff(date.mdy(11,10,2038), date.mdy(7,18,2094), 'days') => -20339.00
datediff(date.mdy(2,29,1900), date.mdy(2,29,1904), 'days') => -1460.00
datediff(date.mdy(2,29,1904), date.mdy(2,29,1908), 'days') => -1461.00
datediff(date.mdy(2,29,1900), date.mdy(2,28,1903), 'days') => -1094.00

datediff(date.mdy(6,30,1680), date.mdy(6,10,1648), 'years') => 32.00
datediff(date.mdy(7,24,1716), date.mdy(6,30,1680), 'years') => 36.00
datediff(date.mdy(6,19,1768), date.mdy(7,24,1716), 'years') => 51.00
datediff(date.mdy(8,2,1819), date.mdy(6,19,1768), 'years') => 51.00
datediff(date.mdy(3,27,1839), date.mdy(8,2,1819), 'years') => 19.00
datediff(date.mdy(4,19,1903), date.mdy(3,27,1839), 'years') => 64.00
datediff(date.mdy(8,25,1929), date.mdy(4,19,1903), 'years') => 26.00
datediff(date.mdy(9,29,1941), date.mdy(8,25,1929), 'years') => 12.00
datediff(date.mdy(4,19,1943), date.mdy(9,29,1941), 'years') => 1.00
datediff(date.mdy(10,7,1943), date.mdy(4,19,1943), 'years') => 0.00
datediff(date.mdy(3,17,1992), date.mdy(10,7,1943), 'years') => 48.00
datediff(date.mdy(2,25,1996), date.mdy(3,17,1992), 'years') => 3.00
datediff(date.mdy(2,25,1996), date.mdy(9,29,41), 'years') => 54.00
datediff(date.mdy(4,19,43), date.mdy(9,29,41), 'years') => 1.00
datediff(date.mdy(10,7,43), date.mdy(4,19,43), 'years') => 0.00
datediff(date.mdy(3,17,92), date.mdy(10,7,43), 'years') => 48.00
datediff(date.mdy(2,25,96), date.mdy(3,17,92), 'years') => 3.00
datediff(date.mdy(11,10,2038), date.mdy(2,25,96), 'years') => 42.00
datediff(date.mdy(7,18,2094), date.mdy(11,10,2038), 'years') => 55.00
datediff(date.mdy(2,29,1904), date.mdy(2,29,1900), 'years') => 3.00
datediff(date.mdy(2,29,1908), date.mdy(2,29,1904), 'years') => 4.00
datediff(date.mdy(2,28,1903), date.mdy(2,29,1900), 'years') => 2.00

datediff(date.mdy(6,30,1680), date.mdy(6,10,1648), 'months') => 384.00
datediff(date.mdy(7,24,1716), date.mdy(6,30,1680), 'months') => 432.00
datediff(date.mdy(6,19,1768), date.mdy(7,24,1716), 'months') => 622.00
datediff(date.mdy(8,2,1819), date.mdy(6,19,1768), 'months') => 613.00
datediff(date.mdy(3,27,1839), date.mdy(8,2,1819), 'months') => 235.00
datediff(date.mdy(4,19,1903), date.mdy(3,27,1839), 'months') => 768.00
datediff(date.mdy(8,25,1929), date.mdy(4,19,1903), 'months') => 316.00
datediff(date.mdy(9,29,1941), date.mdy(8,25,1929), 'months') => 145.00
datediff(date.mdy(4,19,1943), date.mdy(9,29,1941), 'months') => 18.00
datediff(date.mdy(10,7,1943), date.mdy(4,19,1943), 'months') => 5.00
datediff(date.mdy(3,17,1992), date.mdy(10,7,1943), 'months') => 581.00
datediff(date.mdy(2,25,1996), date.mdy(3,17,1992), 'months') => 47.00
datediff(date.mdy(2,25,1996), date.mdy(9,29,41), 'months') => 652.00
datediff(date.mdy(4,19,43), date.mdy(9,29,41), 'months') => 18.00
datediff(date.mdy(10,7,43), date.mdy(4,19,43), 'months') => 5.00
datediff(date.mdy(3,17,92), date.mdy(10,7,43), 'months') => 581.00
datediff(date.mdy(2,25,96), date.mdy(3,17,92), 'months') => 47.00
datediff(date.mdy(11,10,2038), date.mdy(2,25,96), 'months') => 512.00
datediff(date.mdy(7,18,2094), date.mdy(11,10,2038), 'months') => 668.00
datediff(date.mdy(2,29,1904), date.mdy(2,29,1900), 'months') => 47.00
datediff(date.mdy(2,29,1908), date.mdy(2,29,1904), 'months') => 48.00
datediff(date.mdy(2,28,1903), date.mdy(2,29,1900), 'months') => 35.00

datediff(date.mdy(6,30,1680), date.mdy(6,10,1648), 'quarters') => 128.00
datediff(date.mdy(7,24,1716), date.mdy(6,30,1680), 'quarters') => 144.00
datediff(date.mdy(6,19,1768), date.mdy(7,24,1716), 'quarters') => 207.00
datediff(date.mdy(8,2,1819), date.mdy(6,19,1768), 'quarters') => 204.00
datediff(date.mdy(3,27,1839), date.mdy(8,2,1819), 'quarters') => 78.00
datediff(date.mdy(4,19,1903), date.mdy(3,27,1839), 'quarters') => 256.00
datediff(date.mdy(8,25,1929), date.mdy(4,19,1903), 'quarters') => 105.00
datediff(date.mdy(9,29,1941), date.mdy(8,25,1929), 'quarters') => 48.00
datediff(date.mdy(4,19,1943), date.mdy(9,29,1941), 'quarters') => 6.00
datediff(date.mdy(10,7,1943), date.mdy(4,19,1943), 'quarters') => 1.00
datediff(date.mdy(3,17,1992), date.mdy(10,7,1943), 'quarters') => 193.00
datediff(date.mdy(2,25,1996), date.mdy(3,17,1992), 'quarters') => 15.00
datediff(date.mdy(2,25,1996), date.mdy(9,29,41), 'quarters') => 217.00
datediff(date.mdy(4,19,43), date.mdy(9,29,41), 'quarters') => 6.00
datediff(date.mdy(10,7,43), date.mdy(4,19,43), 'quarters') => 1.00
datediff(date.mdy(3,17,92), date.mdy(10,7,43), 'quarters') => 193.00
datediff(date.mdy(2,25,96), date.mdy(3,17,92), 'quarters') => 15.00
datediff(date.mdy(11,10,2038), date.mdy(2,25,96), 'quarters') => 170.00
datediff(date.mdy(7,18,2094), date.mdy(11,10,2038), 'quarters') => 222.00
datediff(date.mdy(2,29,1904), date.mdy(2,29,1900), 'quarters') => 15.00
datediff(date.mdy(2,29,1908), date.mdy(2,29,1904), 'quarters') => 16.00
datediff(date.mdy(2,28,1903), date.mdy(2,29,1900), 'quarters') => 11.00

# DATESUM with non-leap year
ctime.days(datesum(date.mdy(1,31,1900), 1, 'months') - date.mdy(1,1,1900)) => 58.00
ctime.days(datesum(date.mdy(1,31,1900), 2, 'months') - date.mdy(1,1,1900)) => 89.00
ctime.days(datesum(date.mdy(1,31,1900), 3, 'months') - date.mdy(1,1,1900)) => 119.00
ctime.days(datesum(date.mdy(1,31,1900), 4, 'months') - date.mdy(1,1,1900)) => 150.00
ctime.days(datesum(date.mdy(1,31,1900), 5.4, 'months') - date.mdy(1,1,1900)) => 180.00
ctime.days(datesum(date.mdy(1,31,1900), 6, 'months') - date.mdy(1,1,1900)) => 211.00
ctime.days(datesum(date.mdy(1,31,1900), 7, 'months') - date.mdy(1,1,1900)) => 242.00
ctime.days(datesum(date.mdy(1,31,1900), 8, 'months') - date.mdy(1,1,1900)) => 272.00
ctime.days(datesum(date.mdy(1,31,1900), 9, 'months') - date.mdy(1,1,1900)) => 303.00
ctime.days(datesum(date.mdy(1,31,1900), 10, 'months') - date.mdy(1,1,1900)) => 333.00
ctime.days(datesum(date.mdy(1,31,1900), 11, 'months') - date.mdy(1,1,1900)) => 364.00
ctime.days(datesum(date.mdy(1,31,1900), 12, 'months') - date.mdy(1,1,1900)) => 395.00
ctime.days(datesum(date.mdy(1,31,1900), 13.9, 'months') - date.mdy(1,1,1900)) => 423.00
ctime.days(datesum(date.mdy(1,31,1900), 1, 'months', 'rollover') - date.mdy(1,1,1900)) => 61.00
ctime.days(datesum(date.mdy(1,31,1900), 2, 'months', 'rollover') - date.mdy(1,1,1900)) => 89.00
ctime.days(datesum(date.mdy(1,31,1900), 3.2, 'months', 'rollover') - date.mdy(1,1,1900)) => 120.00
ctime.days(datesum(date.mdy(1,31,1900), 4, 'months', 'rollover') - date.mdy(1,1,1900)) => 150.00
ctime.days(datesum(date.mdy(1,31,1900), 5, 'months', 'rollover') - date.mdy(1,1,1900)) => 181.00
ctime.days(datesum(date.mdy(1,31,1900), 6, 'months', 'rollover') - date.mdy(1,1,1900)) => 211.00
ctime.days(datesum(date.mdy(1,31,1900), 7, 'months', 'rollover') - date.mdy(1,1,1900)) => 242.00
ctime.days(datesum(date.mdy(1,31,1900), 8, 'months', 'rollover') - date.mdy(1,1,1900)) => 273.00
ctime.days(datesum(date.mdy(1,31,1900), 9, 'months', 'rollover') - date.mdy(1,1,1900)) => 303.00
ctime.days(datesum(date.mdy(1,31,1900), 10, 'months', 'rollover') - date.mdy(1,1,1900)) => 334.00
ctime.days(datesum(date.mdy(1,31,1900), 11, 'months', 'rollover') - date.mdy(1,1,1900)) => 364.00
ctime.days(datesum(date.mdy(1,31,1900), 12, 'months', 'rollover') - date.mdy(1,1,1900)) => 395.00
ctime.days(datesum(date.mdy(1,31,1900), 13, 'months', 'rollover') - date.mdy(1,1,1900)) => 426.00

# DATESUM with leap year
ctime.days(datesum(date.mdy(1,31,1904), 1, 'months') - date.mdy(1,1,1904)) => 59.00
ctime.days(datesum(date.mdy(1,31,1904), 2.5, 'months') - date.mdy(1,1,1904)) => 90.00
ctime.days(datesum(date.mdy(1,31,1904), 3, 'months') - date.mdy(1,1,1904)) => 120.00
ctime.days(datesum(date.mdy(1,31,1904), 4.9, 'months') - date.mdy(1,1,1904)) => 151.00
ctime.days(datesum(date.mdy(1,31,1904), 5.1, 'months') - date.mdy(1,1,1904)) => 181.00
ctime.days(datesum(date.mdy(1,31,1904), 6, 'months') - date.mdy(1,1,1904)) => 212.00
ctime.days(datesum(date.mdy(1,31,1904), 7, 'months') - date.mdy(1,1,1904)) => 243.00
ctime.days(datesum(date.mdy(1,31,1904), 8, 'months') - date.mdy(1,1,1904)) => 273.00
ctime.days(datesum(date.mdy(1,31,1904), 9, 'months') - date.mdy(1,1,1904)) => 304.00
ctime.days(datesum(date.mdy(1,31,1904), 10, 'months') - date.mdy(1,1,1904)) => 334.00
ctime.days(datesum(date.mdy(1,31,1904), 11, 'months') - date.mdy(1,1,1904)) => 365.00
ctime.days(datesum(date.mdy(1,31,1904), 12, 'months') - date.mdy(1,1,1904)) => 396.00
ctime.days(datesum(date.mdy(1,31,1904), 13, 'months') - date.mdy(1,1,1904)) => 424.00
ctime.days(datesum(date.mdy(1,31,1904), 1, 'months', 'rollover') - date.mdy(1,1,1904)) => 61.00
ctime.days(datesum(date.mdy(1,31,1904), 2, 'months', 'rollover') - date.mdy(1,1,1904)) => 90.00
ctime.days(datesum(date.mdy(1,31,1904), 3, 'months', 'rollover') - date.mdy(1,1,1904)) => 121.00
ctime.days(datesum(date.mdy(1,31,1904), 4, 'months', 'rollover') - date.mdy(1,1,1904)) => 151.00
ctime.days(datesum(date.mdy(1,31,1904), 5, 'months', 'rollover') - date.mdy(1,1,1904)) => 182.00
ctime.days(datesum(date.mdy(1,31,1904), 6, 'months', 'rollover') - date.mdy(1,1,1904)) => 212.00
ctime.days(datesum(date.mdy(1,31,1904), 7, 'months', 'rollover') - date.mdy(1,1,1904)) => 243.00
ctime.days(datesum(date.mdy(1,31,1904), 8, 'months', 'rollover') - date.mdy(1,1,1904)) => 274.00
ctime.days(datesum(date.mdy(1,31,1904), 9, 'months', 'rollover') - date.mdy(1,1,1904)) => 304.00
ctime.days(datesum(date.mdy(1,31,1904), 10, 'months', 'rollover') - date.mdy(1,1,1904)) => 335.00
ctime.days(datesum(date.mdy(1,31,1904), 11, 'months', 'rollover') - date.mdy(1,1,1904)) => 365.00
ctime.days(datesum(date.mdy(1,31,1904), 12, 'months', 'rollover') - date.mdy(1,1,1904)) => 396.00
ctime.days(datesum(date.mdy(1,31,1904), 13, 'months', 'rollover') - date.mdy(1,1,1904)) => 427.00

ctime.days(datesum(date.mdy(6,10,1648), 1, 'weeks') - date.mdy(6,10,1648)) => 7.00
ctime.days(datesum(date.mdy(6,30,1680), 2.5, 'weeks') - date.mdy(6,30,1680)) => 17.50
ctime.days(datesum(date.mdy(7,24,1716), -3, 'weeks') - date.mdy(7,24,1716)) => -21.00
ctime.days(datesum(date.mdy(6,19,1768), 4, 'weeks') - date.mdy(6,19,1768)) => 28.00
ctime.days(datesum(date.mdy(8,2,1819), 5, 'weeks') - date.mdy(8,2,1819)) => 35.00

ctime.days(datesum(date.mdy(6,10,1648), 1, 'days') - date.mdy(6,10,1648)) => 1.00
ctime.days(datesum(date.mdy(6,30,1680), 2.5, 'days') - date.mdy(6,30,1680)) => 2.50
ctime.days(datesum(date.mdy(7,24,1716), -3, 'days') - date.mdy(7,24,1716)) => -3.00
ctime.days(datesum(date.mdy(6,19,1768), 4, 'days') - date.mdy(6,19,1768)) => 4.00
ctime.days(datesum(date.mdy(8,2,1819), 5, 'days') - date.mdy(8,2,1819)) => 5.00

ctime.days(datesum(date.mdy(6,10,1648), 1, 'hours') - date.mdy(6,10,1648)) => 0.04
ctime.days(datesum(date.mdy(6,30,1680), 2.5, 'hours') - date.mdy(6,30,1680)) => 0.10
ctime.days(datesum(date.mdy(6,19,1768), -4, 'hours') - date.mdy(6,19,1768)) => -0.17
ctime.days(datesum(date.mdy(8,2,1819), 5, 'hours') - date.mdy(8,2,1819)) => 0.21

# These test values are from Applied Statistics, Algorithm AS 310.
1000 * ncdf.beta(.868,10,20,150) => 937.66
1000 * ncdf.beta(.9,10,10,120) => 730.68
1000 * ncdf.beta(.88,15,5,80) => 160.43
1000 * ncdf.beta(.85,20,10,110) => 186.75
1000 * ncdf.beta(.66,20,30,65) => 655.94
1000 * ncdf.beta(.72,20,50,130) => 979.69
1000 * ncdf.beta(.72,30,20,80) => 116.24
1000 * ncdf.beta(.8,30,40,130) => 993.04

# FIXME: LAG

(X = 1.00); X => 1.00
SYSMIS(1) => false
SYSMIS($SYSMIS) => true
SYSMIS(1 + $SYSMIS) => true

# FIXME: out-of-range and nearly out-of-range values on dates

# Tests correctness of generic optimizations in optimize_tree().
(X = 10.00); x + 0 => 10.00
(X = -3.00); x - 0 => -3.00
(X = 5.00); 0 + x => 5.00
(X = 10.00); x * 1 => 10.00
(X = -3.00); 1 * x => -3.00
(X = 5.00); x / 1 => 5.00
(X = 10.00); 0 * x => 0.00
(X = -3.00); x * 0 => 0.00
(X = 5.00); 0 / x => 0.00
(X = 5.00); mod(0, x) => 0.00
(X = 5.00); x ** 1 => 5.00
(X = 5.00); x ** 2 => 25.00
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create optimizing input"
echo 'set mxwarn 1000.
set mxerr 1000.' > $TEMPDIR/expr-opt.stat
sed < $TEMPDIR/expr-list >> $TEMPDIR/expr-opt.stat \
	-e 's#^\(\(.*\); \)*\(.*\) => .*$#DEBUG EVALUATE\2/\3.#'
if [ $? -ne 0 ] ; then no_result ; fi

activity="run optimizing program"
$SUPERVISOR $PSPP --testing-mode -o pspp.csv \
	 $TEMPDIR/expr-opt.stat >$TEMPDIR/expr-opt.err 2> $TEMPDIR/expr-opt.out

activity="compare optimizing output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/expr-list $TEMPDIR/expr-opt.out
diff -b $TEMPDIR/expr-list $TEMPDIR/expr-opt.out
if [ $? -ne 0 ] ; then fail ; fi

activity="create non-optimizing input"
echo 'set mxwarn 1000.
set mxerr 1000.' > $TEMPDIR/expr-noopt.stat
sed < $TEMPDIR/expr-list >> $TEMPDIR/expr-noopt.stat \
	-e 's#^\(\(.*\); \)*\(.*\) => .*$#DEBUG EVALUATE NOOPTIMIZE\2/\3.#'
if [ $? -ne 0 ] ; then no_result ; fi

activity="run non-optimizing program"
$SUPERVISOR $PSPP --testing-mode -o pspp.csv \
	$TEMPDIR/expr-noopt.stat >$TEMPDIR/expr-noopt.err 2> $TEMPDIR/expr-noopt.out

activity="compare non-optimizing output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/expr-list $TEMPDIR/expr-noopt.out
diff -b $TEMPDIR/expr-list $TEMPDIR/expr-noopt.out
if [ $? -ne 0 ] ; then fail ; fi

activity="create optimizing postfix input"
echo 'set mxwarn 1000.
set mxerr 1000.' > $TEMPDIR/expr-opt-pos.stat
sed < $TEMPDIR/expr-list >> $TEMPDIR/expr-opt-pos.stat \
	-e 's#^\(\(.*\); \)*\(.*\) => .*$#DEBUG EVALUATE POSTFIX\2/\3.#'
if [ $? -ne 0 ] ; then no_result ; fi

activity="run optimizing postfix program"
$SUPERVISOR $PSPP --testing-mode -o pspp.csv \
	 $TEMPDIR/expr-opt-pos.stat >$TEMPDIR/expr-opt-pos.err 2> $TEMPDIR/expr-opt-pos.out
if [ $? -eq 0 ] ; then no_result ; fi

activity="create non-optimizing postfix input"
echo 'set mxwarn 1000.
set mxerr 1000.' > $TEMPDIR/expr-noopt-pos.stat
sed < $TEMPDIR/expr-list >> $TEMPDIR/expr-noopt-pos.stat \
	-e 's#^\(\(.*\); \)*\(.*\) => .*$#DEBUG EVALUATE NOOPTIMIZE POSTFIX\2/\3.#'
if [ $? -ne 0 ] ; then no_result ; fi

activity="run non-optimizing postfix program"
$SUPERVISOR $PSPP --testing-mode -o pspp.csv \
	$TEMPDIR/expr-noopt-pos.stat >$TEMPDIR/expr-noopt-pos.err 2> $TEMPDIR/expr-noopt-pos.out
if [ $? -eq 0 ] ; then no_result ; fi

pass
