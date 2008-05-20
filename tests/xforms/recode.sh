#!/bin/sh

# This program tests the RECODE command

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps


# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH

LANG=C
export LANG

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

activity="create program"
cat > $TESTFILE <<EOF
DATA LIST LIST NOTABLE/x (f1) s (a4) t (a10).
MISSING VALUES x(9)/s('xxx').
BEGIN DATA.
0, '', ''
1, a, a
2, ab, ab
3, abc, abc
4, abcd, abcd
5, 123, 123
6, ' 123', ' 123'
7, +1, +1
8, 1x, 1x
9, abcd, abcdefghi
,  xxx, abcdefghij
END DATA.

* Numeric to numeric, without INTO.
NUMERIC x0 TO x8 (F3).
MISSING VALUES x0 to x8 (9).
COMPUTE x0=value(x).
RECODE x0 (1=9).
COMPUTE x1=value(x).
RECODE x1 (1=9)(3=8)(5=7).
COMPUTE x2=value(x).
RECODE x2 (1=8)(2,3,4,5,6,8=9)(9=1).
COMPUTE x3=value(x).
RECODE x3 (1 THRU 9=10)(MISSING=11).
COMPUTE x4=value(x).
RECODE x4 (MISSING=11)(1 THRU 9=10).
COMPUTE x5=value(x).
RECODE x5 (LOWEST THRU 5=1).
COMPUTE x6=value(x).
RECODE x6 (4 THRU HIGHEST=2).
COMPUTE x7=value(x).
RECODE x7 (LO THRU HI=3).
COMPUTE x8=value(x).
RECODE x8 (SYSMIS=4).
LIST x x0 TO x8.

* Numeric to numeric, with INTO, without COPY.
NUMERIC ix0 TO ix8 (F3).
RECODE x (1=9) INTO ix0.
RECODE x (1=9)(3=8)(5=7) INTO ix1.
RECODE x (1=8)(2,3,4,5,6,8=9)(9=1) INTO ix2.
RECODE x (1 THRU 9=10)(MISSING=11) INTO ix3.
RECODE x (MISSING=11)(1 THRU 9=10) INTO ix4.
RECODE x (LOWEST THRU 5=1) INTO ix5.
RECODE x (4 THRU HIGHEST=2) INTO ix6.
RECODE x (LO THRU HI=3) INTO ix7.
RECODE x (SYSMIS=4) INTO ix8.
LIST x ix0 TO ix8.

* Numeric to numeric, with INTO, with COPY.
NUMERIC cx0 TO cx8 (F3).
RECODE x (1=9)(ELSE=COPY) INTO cx0.
RECODE x (1=9)(3=8)(5=7)(ELSE=COPY) INTO cx1.
RECODE x (1=8)(2,3,4,5,6,8=9)(9=1)(ELSE=COPY) INTO cx2.
RECODE x (1 THRU 9=10)(MISSING=11)(ELSE=COPY) INTO cx3.
RECODE x (MISSING=11)(1 THRU 9=10)(ELSE=COPY) INTO cx4.
RECODE x (LOWEST THRU 5=1)(ELSE=COPY) INTO cx5.
RECODE x (4 THRU HIGHEST=2)(ELSE=COPY) INTO cx6.
RECODE x (LO THRU HI=3)(ELSE=COPY) INTO cx7.
RECODE x (SYSMIS=4)(ELSE=COPY) INTO cx8.
LIST x cx0 TO cx8.

* String to string, with INTO, without COPY.
STRING s0 TO s2 (A4)/t0 TO t3 (A10).
RECODE s t ('a'='b')('ab'='bc') INTO s0 t0.
RECODE s t ('abcd'='xyzw') INTO s1 t1.
RECODE s t ('abc'='def')(ELSE='xyz') INTO s2 t2.
RECODE t ('a'='b')('abcdefghi'='xyz')('abcdefghij'='jklmnopqr') INTO t3.
LIST s t s0 TO s2 t0 TO t3.

* String to string, with INTO, with COPY.
STRING cs0 TO cs2 (A4)/ct0 TO ct3 (A10).
RECODE s t ('a'='b')('ab'='bc')(ELSE=COPY) INTO cs0 ct0.
RECODE s t ('abcd'='xyzw')(ELSE=COPY) INTO cs1 ct1.
RECODE s t ('abc'='def')(ELSE='xyz')(ELSE=COPY) INTO cs2 ct2.
RECODE t ('a'='b')('abcdefghi'='xyz')('abcdefghij'='jklmnopqr')(ELSE=COPY)
    INTO ct3.
LIST s t cs0 TO cs2 ct0 TO ct3.

* String to numeric.
NUMERIC ns0 TO ns2 (F3)/nt0 TO nt2 (F3).
RECODE s t (CONVERT)(' '=0)('abcd'=1) INTO ns0 nt0.
RECODE s t (' '=0)(CONVERT)('abcd'=1) INTO ns1 nt1.
RECODE s t ('1x'=1)('abcd'=2)(ELSE=3) INTO ns2 nt2.
LIST s t ns0 TO ns2 nt0 TO nt2.

* Numeric to string.
STRING sx0 TO sx2 (a10).
RECODE x (1 THRU 9='abcdefghij') INTO sx0.
RECODE x (0,1,3,5,7,MISSING='xxx') INTO sx1.
RECODE x (2 THRU 6,SYSMIS='xyz')(ELSE='foobar') INTO sx2.
LIST x sx0 TO sx2.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="test output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -bu $TEMPDIR/pspp.list - <<EOF
x  x0  x1  x2  x3  x4  x5  x6  x7  x8
- --- --- --- --- --- --- --- --- ---
0   0   0   0   0   0   1   0   3   0
1   9   9   8  10  10   1   1   3   1
2   2   2   9  10  10   1   2   3   2
3   3   8   9  10  10   1   3   3   3
4   4   4   9  10  10   1   2   3   4
5   5   7   9  10  10   1   2   3   5
6   6   6   9  10  10   6   2   3   6
7   7   7   7  10  10   7   2   3   7
8   8   8   9  10  10   8   2   3   8
9   9   9   1  10  11   9   2   3   9
.   .   .   .  11  11   .   .   .   4
x ix0 ix1 ix2 ix3 ix4 ix5 ix6 ix7 ix8
- --- --- --- --- --- --- --- --- ---
0   .   .   .   .   .   1   .   3   .
1   9   9   8  10  10   1   .   3   .
2   .   .   9  10  10   1   .   3   .
3   .   8   9  10  10   1   .   3   .
4   .   .   9  10  10   1   2   3   .
5   .   7   9  10  10   1   2   3   .
6   .   .   9  10  10   .   2   3   .
7   .   .   .  10  10   .   2   3   .
8   .   .   9  10  10   .   2   3   .
9   .   .   1  10  11   .   2   3   .
.   .   .   .  11  11   .   .   .   4
x cx0 cx1 cx2 cx3 cx4 cx5 cx6 cx7 cx8
- --- --- --- --- --- --- --- --- ---
0   0   0   0   0   0   1   0   3   0
1   9   9   8  10  10   1   1   3   1
2   2   2   9  10  10   1   2   3   2
3   3   8   9  10  10   1   3   3   3
4   4   4   9  10  10   1   2   3   4
5   5   7   9  10  10   1   2   3   5
6   6   6   9  10  10   6   2   3   6
7   7   7   7  10  10   7   2   3   7
8   8   8   9  10  10   8   2   3   8
9   9   9   1  10  11   9   2   3   9
.   .   .   .  11  11   .   .   .   4
   s          t   s0   s1   s2         t0         t1         t2         t3
---- ---------- ---- ---- ---- ---------- ---------- ---------- ----------
                          xyz                        xyz
a    a          b         xyz  b                     xyz        b
ab   ab         bc        xyz  bc                    xyz
abc  abc                  def                        def
abcd abcd            xyzw xyz             xyzw       xyz
123  123                  xyz                        xyz
 123  123                 xyz                        xyz
+1   +1                   xyz                        xyz
1x   1x                   xyz                        xyz
abcd abcdefghi       xyzw xyz                        xyz        xyz
xxx  abcdefghij           xyz                        xyz        jklmnopqr
   s          t  cs0  cs1  cs2        ct0        ct1        ct2        ct3
---- ---------- ---- ---- ---- ---------- ---------- ---------- ----------
                          xyz                        xyz
a    a          b    a    xyz  b          a          xyz        b
ab   ab         bc   ab   xyz  bc         ab         xyz        ab
abc  abc        abc  abc  def  abc        abc        def        abc
abcd abcd       abcd xyzw xyz  abcd       xyzw       xyz        abcd
123  123        123  123  xyz  123        123        xyz        123
 123  123        123  123 xyz   123        123       xyz         123
+1   +1         +1   +1   xyz  +1         +1         xyz        +1
1x   1x         1x   1x   xyz  1x         1x         xyz        1x
abcd abcdefghi  abcd xyzw xyz  abcdefghi  abcdefghi  xyz        xyz
xxx  abcdefghij xxx  xxx  xyz  abcdefghij abcdefghij xyz        jklmnopqr
   s          t ns0 ns1 ns2 nt0 nt1 nt2
---- ---------- --- --- --- --- --- ---
                  .   0   3   .   0   3
a    a            .   .   3   .   .   3
ab   ab           .   .   3   .   .   3
abc  abc          .   .   3   .   .   3
abcd abcd         1   1   2   1   1   2
123  123        123 123   3 123 123   3
 123  123       123 123   3 123 123   3
+1   +1           1   1   3   1   1   3
1x   1x           .   .   1   .   .   1
abcd abcdefghi    1   1   2   .   .   3
xxx  abcdefghij   .   .   3   .   .   3
x        sx0        sx1        sx2
- ---------- ---------- ----------
0            xxx        foobar
1 abcdefghij xxx        foobar
2 abcdefghij            xyz
3 abcdefghij xxx        xyz
4 abcdefghij            xyz
5 abcdefghij xxx        xyz
6 abcdefghij            xyz
7 abcdefghij xxx        foobar
8 abcdefghij            foobar
9 abcdefghij xxx        foobar
.            xxx        xyz
EOF

if [ $? -ne 0 ] ; then fail ; fi

pass



