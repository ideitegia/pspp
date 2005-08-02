#! /bin/sh

# Tests the expression optimizer and evaluator.

TEMPDIR=/tmp/pspp-tst-$$

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


cleanup()
{
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
cat > 00-num-syn.expr <<'EOF'
# Test numeric syntax.
1e2 => 100.00
1e+2 => 100.00
1e-2 => 0.01
1e-99 => 0.00
EOF

cat > 01-conv-to-num.expr <<'EOF'
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
EOF

cat > 02-add-sub.expr <<'EOF'
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
EOF

cat > 03-mul-div.expr <<'EOF'
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
EOF

cat > 04-exp.expr <<'EOF'
# Exponentiation.
2**8 => 256.00
(2**3)**4 => 4096.00	# Irritating, but compatible.
2**3**4 => 4096.00
EOF

cat > 05-negation.expr <<'EOF'
# Unary minus.
2+-3 => -1.00
2*-3 => -6.00
-3**2 => -9.00
(-3)**2 => 9.00
2**-1 => 0.50
0**0 => sysmis
0**-1 => sysmis
(-3)**1.5 => sysmis
EOF

cat > 06-logical-and.expr <<'EOF'
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
EOF

cat > 07-logical-or.expr <<'EOF'
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
EOF

cat > 08-logical-not.expr <<'EOF'
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
EOF

cat > 09-eq.expr <<'EOF'
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
EOF

cat > 10-le.expr <<'EOF'
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
EOF

cat > 11-lt.expr <<'EOF'
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
EOF

cat > 12-ge.expr <<'EOF'
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
EOF

cat > 13-gt.expr <<'EOF'
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
EOF

cat > 14-ne.expr <<'EOF'
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
EOF

cat > 15-exp-log.expr <<'EOF'
exp(10) => 22026.47
exp('x') => error

lg10(500) => 2.70
lg10('x') => error

ln(10) => 2.30
ln('x') => error
EOF

cat > 16-sqrt-abs-mod.expr <<'EOF'
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
EOF

cat > 17-rnd-trunc.expr <<'EOF'
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
EOF

cat > 18-arc.expr <<'EOF'
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
EOF

cat > 19-cos-sin-tan.expr <<'EOF'
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
EOF

cat > 20-missing.expr <<'EOF'
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
EOF

cat > 21-any.expr <<'EOF'
any($sysmis, 1, $sysmis, 3) => sysmis
any(1, 1, 2, 3) => true
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
EOF

cat > 22-range.expr <<'EOF'
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
EOF

cat > 23-max-min.expr <<'EOF'
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
EOF

cat > 24-moments.expr <<'EOF'
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

mean(1, 2, 3, 4, 5) => 3.00
mean(1, $sysmis, 2, 3, $sysmis, 4, 5) => 3.00
mean(1, 2) => 1.50
mean() => error
mean(1) => 1.00
mean(1, $sysmis) => 1.00
mean(1, 2, 3, $sysmis) => 2.00
mean.4(1, 2, 3, $sysmis) => sysmis
mean.4(1, 2, 3) => error

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
EOF

cat > 25-concat.expr <<'EOF'
concat('') => ""
concat('a', 'b') => "ab"
concat('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h') => "abcdefgh"
concat('abcdefgh', 'ijklmnopq') => "abcdefghijklmnopq"
concat('a', 1) => error
concat(1, 2) => error
EOF

cat > 26-index.expr <<'EOF'
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
EOF

cat > 27-rindex.expr <<'EOF'
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
EOF

cat > 28-length.expr <<'EOF'
length('') => 0.00
length('a') => 1.00
length('xy') => 2.00
length('adsf    ') => 8.00
length('abcdefghijkl') => 12.00
length(0) => error
length($sysmis) => error
EOF

cat > 29-pad.expr <<'EOF'
lpad('abc', -1) => ""
lpad('abc', 0) => "abc"
lpad('abc', 2) => "abc"
lpad('abc', 3) => "abc"
lpad('abc', 10) => "       abc"
lpad('abc', 256) => ""
lpad('abc', $sysmis) => ""
lpad('abc', -1, '*') => ""
lpad('abc', 0, '*') => "abc"
lpad('abc', 2, '*') => "abc"
lpad('abc', 3, '*') => "abc"
lpad('abc', 10, '*') => "*******abc"
lpad('abc', 256, '*') => ""
lpad('abc', $sysmis, '*') => ""
lpad('abc', $sysmis, '') => ""
lpad('abc', $sysmis, 'xy') => ""
lpad(0, 10) => error
lpad('abc', 'def') => error
lpad(0, 10, ' ') => error
lpad('abc', 'def', ' ') => error
lpad('x', 5, 0) => error
lpad('x', 5, 2) => error

rpad('abc', -1) => ""
rpad('abc', 0) => "abc"
rpad('abc', 2) => "abc"
rpad('abc', 3) => "abc"
rpad('abc', 10) => "abc       "
rpad('abc', 256) => ""
rpad('abc', $sysmis) => ""
rpad('abc', -1, '*') => ""
rpad('abc', 0, '*') => "abc"
rpad('abc', 2, '*') => "abc"
rpad('abc', 3, '*') => "abc"
rpad('abc', 10, '*') => "abc*******"
rpad('abc', 256, '*') => ""
rpad('abc', $sysmis, '*') => ""
rpad('abc', $sysmis, '') => ""
rpad('abc', $sysmis, 'xy') => ""
rpad(0, 10) => error
rpad('abc', 'def') => error
rpad(0, 10, ' ') => error
rpad('abc', 'def', ' ') => error
rpad('x', 5, 0) => error
rpad('x', 5, 2) => error
EOF

cat > 30-num-str.expr <<'EOF'
number("123", f3.0) => 123.00
number(" 123", f3.0) => 12.00
number("123", f3.1) => 12.30
number("   ", f3.1) => sysmis

string(123.56, f5.1) => "123.6"
string($sysmis, f5.1) => "   . "
string("abc", A5) => error
EOF

cat > 31-trim.expr <<'EOF'
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
EOF

cat > 32-substr.expr <<'EOF'
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
EOF

cat > 33-case.expr <<'EOF'
lower('ABCDEFGHIJKLMNOPQRSTUVWXYZ!@%&*(089') => "abcdefghijklmnopqrstuvwxyz!@%&*(089"
lower('') => ""
lower(1) => error

upcase('abcdefghijklmnopqrstuvwxyz!@%&*(089') => "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@%&*(089"
upcase('') => ""
upcase(1) => error
EOF

cat > 34-time.expr <<'EOF'
time.days(1) => 86400.00
time.days(-1) => -86400.00
time.days(0.5) => 43200.00
time.days('x') => error
time.days($sysmis) => sysmis

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
EOF

cat > 35-ctime.expr <<'EOF'
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
EOF

# FIXME: XDATE.* functions
# FIXME: LAG
# FIXME: YRMODA

printf "expressions..."
for d in *.expr; do
    base=`echo $d | sed 's/\.expr$//'`

    # Remove comments.
    sed -ne 's/#.*//;/^[ 	]*$/!p' < $base.expr > $base.clean
    if [ $? -ne 0 ] ; then no_result ; fi

    for optimize in opt noopt; do
	if test optimize = opt; then
	    opt_kw=''
	else
	    opt_kw=' NOOPTIMIZE'
	fi
	
        # Translate to DEBUG EVALUATE commands.
	activity="$base, $optimize: create input"
	sed 's#^\(.*\) => \(.*\)$#DEBUG EVALUATE'"$opt_kw"'/\1.#' \
	    < $base.clean > $base.$optimize.stat
	if [ $? -ne 0 ] ; then no_result ; fi

	# Run.
	activity="$base, $optimize: run program"
	$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii \
	    $base.$optimize.stat > $base.$optimize.err 2> $base.$optimize.out

	# Compare.
	activity="$base, $optimize: compare output"
	diff -B -b $base.clean $base.$optimize.out
	if [ $? -ne 0 ] ; then fail ; fi
    done
    num=`echo $d | sed 's/-.*//'`
    printf " $num"
done
printf ' ...done\n'
pass
