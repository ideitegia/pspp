#! /bin/sh

# Tests floating-point format conversions.

TEMPDIR=/tmp/pspp-tst-$$

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

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
activity="create test program"
sed -e 's/#.*//' \
    -e 's/^[ 	]*//' \
    -e 's/[ 	]*$//' \
    -e 's/^\(..*\)$/DEBUG FLOAT FORMAT \1./' \
    > $TEMPDIR/float-format.pspp <<'EOF'
# Each of the tests below checks that conversion between
# floating-point formats works correctly.  Comparisons that use ==
# require that conversion from any format on the line to any other
# format on the line work losslessly.  Comparisons that use => only
# check that conversions work losslessly in the given direction.

# Key to format names:
# isl: IEEE single-precision, little endian
# isb: IEEE single-precision, big endian
# idl: IEEE double-precision, little endian
# idb: IEEE double-precision, big endian
# vf: VAX F
# vd: VAX D
# vg: VAX G
# zs: Z architecture short
# zl: Z architecture long
# x: hexadecimal digits

# IEEE special values.
 0 == isb(x'00000000')
x('Infinity') == isb(x'7f800000')
x('-Infinity') == isb(x'ff800000')
x('NaN:') => isb(x'7f800001')		# NaN requires nonzero fraction.
x('NaN:e000000000000000') == isb(x'7ff00000') == idb(x'7ffe000000000000')
x('NaN:5a5a5e0000000000') == isb(x'7fad2d2f') == idb(x'7ff5a5a5e0000000')
x('NaN:975612abcdef4000') == idb(x'7ff975612abcdef4')
x('-NaN:e000000000000000') == isb(x'fff00000') == idb(x'fffe000000000000')
x('-NaN:5a5a5e0000000000') == isb(x'ffad2d2f') == idb(x'fff5a5a5e0000000')
x('-NaN:975612abcdef4000') == idb(x'fff975612abcdef4')

# PSPP special values.
x('Missing') == isb(x'ff7fffff') == idb(x'ffefffffffffffff') == isl(x'ffff7fff') == idl(x'ffffffffffffefff') == vf(x'ffffffff') == vd(x'ffffffffffffffff') == vg(x'ffffffffffffffff') == zs(x'ffffffff') == zl(x'ffffffffffffffff')
x('Lowest') == isb(x'ff7ffffe') == idb(x'ffeffffffffffffe') == isl(x'feff7fff') == idl(x'feffffffffffefff') == vf(x'fffffeff') == vd(x'fffffeffffffffff') == vg(x'fffffeffffffffff') == zs(x'fffffffe') == zl(x'fffffffffffffffe')
x('Highest') == isb(x'7f7fffff') == idb(x'7fefffffffffffff') == isl(x'ffff7f7f') == idl(x'ffffffffffffef7f') == vf(x'ff7fffff') == vd(x'ffffffffff7fffff') == vg(x'ffffffffff7fffff') == zs(x'7fffffff') == zl(x'7fffffffffffffff')

# From Wikipedia.
0.15625 == isb(b'00111110001000000000000000000000')
-118.625 == isb(b'11000010111011010100000000000000')

# http://www.psc.edu/general/software/packages/ieee/ieee.html
x('NaN:0400000000000000') == isb(b'01111111100000100000000000000000')
x('-NaN:2225540000000000') == isb(b'11111111100100010001001010101010')
2 == isb(b'01000000000000000000000000000000')
6.5 == isb(b'01000000110100000000000000000000')
-6.5 == isb(b'11000000110100000000000000000000')
x('.4p-124') == isb(b'00000000100000000000000000000000')
x('.2p-124') == isb(b'00000000010000000000000000000000')

# Using converter at http://babbage.cs.qc.edu/IEEE-754/Decimal.html
# plus Emacs 'calc' to convert decimal to hexadecimal
x('.7b74bc6a7ef9db23p8') => isb(x'42f6e979')		# 123.456
x('.7b74bc6a7ef9db23p8') => idb(x'405edd2f1a9fbe77')
x('.817427d2d4642004p-12') => isb(x'39017428')		# .0001234567
x('.817427d2d4642004p-12') => idb(x'3f202e84fa5a8c84')
x('.446c3b15f9926688p168') => isb(x'7f800000')		# 1e50; overflow
x('.446c3b15f9926688p168') => idb(x'4a511b0ec57e649a')

# From multiple editions of the z/Architecture Principles of Operation
# manual.
	      1.0 == zs(x'41100000') == isb(x'3f800000')
	      0.5 == zs(x'40800000') == isb(x'3f000000')
       x('.4p-4') == zs(x'3f400000') == isb(x'3c800000')
		0 == zs(x'00000000') == isb(x'00000000')
	             zs(x'80000000') == isb(x'80000000')
	      -15 == zs(x'c1f00000') == isb(x'c1700000')
# x('.ffffffp252') == zs(x'7fffffff')
      x('.3b4p8') == zs(x'423b4000')
     x('.1p-256') == zs(x'00100000')
     x('.4p-124') == zs(x'21400000') == isb(x'00800000')
     x('.8p-148') == zs(x'1b800000') == isb(x'00000001')
# x('.ffffffp128') == zs(x'60ffffff') == isb(x'7f7fffff')
     x('.1p-256') == zs(x'00100000')
     x('.1p-256') => isb(x'00000000')              # Underflow to zero.
 x('.ffffffp248') == zs(x'7effffff')
 x('.ffffffp248') => isb(x'7f800000')              # Overflow to +Infinity.

            x('.4p-1020') => zl(x'0000000000000000')     # Underflow to zero.
            x('.4p-1020') == idb(x'0010000000000000')
            x('.4p-1072') => zl(x'0000000000000000')     # Underflow to zero.
            x('.4p-1072') => idb(x'0000000000000001')
x('.fffffffffffff8p1024') => zl(x'7fffffffffffffff')     # Overflow to maxval.
x('.fffffffffffff8p1024') => idb(x'7fefffffffffffff')
            x('.1p-256') == zl(x'0010000000000000') == idb(x'2fb0000000000000')
 x('.ffffffffffffffp248') == zl(x'7effffffffffffff')
 x('.ffffffffffffffp248') => idb(x'4f70000000000000')	# Loses precision.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode $TEMPDIR/float-format.pspp
if [ $? -ne 0 ] ; then fail ; fi

pass
