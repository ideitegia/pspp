#!/bin/sh

# This program tests the DO IF command.

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

activity="generate input and expected output"
(for a in 0 1 ' '; do
    for b in 0 1 ' '; do
	for c in 0 1 ' '; do
	    for d in 0 1 ' '; do
		abcd=$a$b$c$d
		echo "$abcd" 1>&3
		if test "$a" = "1"; then
		    echo " $abcd A"
		elif test "$a" = " "; then
		    :
		elif test "$b" = "1"; then
		    echo " $abcd B"
		elif test "$b" = " "; then
		    :
		elif test "$c" = "1"; then
		    echo " $abcd C"
		elif test "$c" = " "; then
		    :
		elif test "$d" = "1"; then
		    echo " $abcd D"
		elif test "$d" = " "; then
		    :
		else
		    echo " $abcd E"
		fi
	    done
	done
    done
done) >test1.expected 3>test1.data
if [ $? -ne 0 ] ; then no_result ; fi

activity="create test1.pspp"
cat > test1.pspp <<EOF
DATA LIST FILE="test1.data"/A B C D 1-4 ABCD 1-4 (A).
DO IF A.
PRINT OUTFILE="test1.out"/ABCD 'A'.
ELSE IF B.
PRINT OUTFILE="test1.out"/ABCD 'B'.
ELSE IF C.
PRINT OUTFILE="test1.out"/ABCD 'C'.
ELSE IF D.
PRINT OUTFILE="test1.out"/ABCD 'D'.
ELSE.
PRINT OUTFILE="test1.out"/ABCD 'E'.
END IF.
EXECUTE.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run test1"
$SUPERVISOR $PSPP -o pspp.csv test1.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare test1 results"
diff -u $TEMPDIR/test1.out $TEMPDIR/test1.expected
if [ $? -ne 0 ] ; then fail ; fi

pass
