#!/bin/sh

# This program tests that system files can be read properly, even when the 
# case_size header value is -1 (Some 3rd party products do this)


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
cat <<EOF > $TESTFILE
GET FILE='$top_srcdir/tests/no_case_size.sav'.
DISPLAY DICTIONARY.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' pspp.list
diff -b -w pspp.list - <<EOF
1.1 DISPLAY.  
+--------+-------------------------------------------+--------+
|Variable|Description                                |Position|
#========#===========================================#========#
|CONT    |continents of the world                    |       1|
|        |Format: A32                                |        |
+--------+-------------------------------------------+--------+
|SIZE    |sq km                                      |       2|
|        |Format: F8.2                               |        |
+--------+-------------------------------------------+--------+
|POP     |population                                 |       3|
|        |Format: F8.2                               |        |
+--------+-------------------------------------------+--------+
|COUNT   |number of countries                        |       4|
|        |Format: F8.2                               |        |
+--------+-------------------------------------------+--------+
                            CONT     SIZE      POP    COUNT
-------------------------------- -------- -------- --------
Asia                             44579000   4E+009    44.00 
Africa                           30065000   8E+008    53.00 
North America                    24256000   5E+008    23.00 
South America                    17819000   3E+008    12.00 
Antarctica                       13209000      .00      .00 
Europe                            9938000   7E+008    46.00 
Australia/Oceania                 7687000 31000000    14.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
