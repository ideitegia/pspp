#!/bin/sh

# This program tests that tab characters can be used in string input

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

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

activity="create program 1"
cat > $TEMPDIR/tabs.stat <<EOF
data list /X 1-80 (a).
begin data.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 2"
printf  "\t1\t12\t123\t1234\t12345\n" >> $TEMPDIR/tabs.stat
if [ $? -ne 0 ] ; then no_result ; fi


activity="create program 3"
cat >> $TEMPDIR/tabs.stat <<EOF
end data.
print /x.
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TEMPDIR/tabs.stat
if [ $? -ne 0 ] ; then no_result ; fi


diff -c $TEMPDIR/pspp.csv - << EOF | perl -e 's/^\s*$//g'
Table: Reading 1 record from INLINE.
Variable,Record,Columns,Format
X,1,1- 80,A80

    1   12  123 1234    12345
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
