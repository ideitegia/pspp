#!/bin/sh

# This program tests that pspp can read gnumeric files

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

# ensure that top_srcdir and top_builddir  are absolute
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
top_srcdir=`cd $top_srcdir; pwd`
top_builddir=`cd $top_builddir; pwd`

PSPP=$top_builddir/src/ui/terminal/pspp

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

activity="zip the gnm file and place it in the test directory"
gzip -c $top_srcdir/tests/Book1.gnm.unzipped > $TEMPDIR/Book1.gnumeric
if [ $? -ne 0 ] ; then no_result ; fi

activity="create program 1"
cat > $TESTFILE <<EOF
GET DATA /TYPE=gnm /FILE='$TEMPDIR/Book1.gnumeric'  /READNAMES=off /SHEET=name 'This' /CELLRANGE=range 'g9:i13' .
DISPLAY VARIABLES.
LIST.


GET DATA /TYPE=gnm /FILE='$TEMPDIR/Book1.gnumeric'  /READNAMES=on /SHEET=name 'This' /CELLRANGE=range 'g8:i13' .
DISPLAY VARIABLES.
LIST.


GET DATA /TYPE=gnm /FILE='$TEMPDIR/Book1.gnumeric' /SHEET=index 3.
DISPLAY VARIABLES.
LIST.

* This sheet has no data in one of its variables
GET DATA /TYPE=gnm /FILE='$TEMPDIR/Book1.gnumeric' /READNAMES=on /SHEET=index 5.
DISPLAY VARIABLES.
LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 1"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare output 1"
diff $TEMPDIR/pspp.list - <<EOF
1.1 DISPLAY.  
+--------+-------------------------------------------+--------+
|Variable|Description                                |Position|
#========#===========================================#========#
|var1    |Format: F8.2                               |       1|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|var2    |Format: A8                                 |       2|
|        |Measure: Nominal                           |        |
|        |Display Alignment: Left                    |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|var3    |Format: F8.2                               |       3|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+

    var1     var2     var3
-------- -------- --------
     .00 fred        20.00 
    1.00 11          21.00 
    2.00 twelve      22.00 
    3.00 13          23.00 
    4.00 14          24.00 

2.1 DISPLAY.  
+--------+-------------------------------------------+--------+
|Variable|Description                                |Position|
#========#===========================================#========#
|V1      |Format: F8.2                               |       1|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|V2      |Format: A8                                 |       2|
|        |Measure: Nominal                           |        |
|        |Display Alignment: Left                    |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|var1    |Format: F8.2                               |       3|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+

      V1       V2     var1
-------- -------- --------
     .00 fred        20.00 
    1.00 11          21.00 
    2.00 twelve      22.00 
    3.00 13          23.00 
    4.00 14          24.00 

3.1 DISPLAY.  
+--------+-------------------------------------------+--------+
|Variable|Description                                |Position|
#========#===========================================#========#
|name    |Format: A8                                 |       1|
|        |Measure: Nominal                           |        |
|        |Display Alignment: Left                    |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|id      |Format: F8.2                               |       2|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|height  |Format: F8.2                               |       3|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+

    name       id   height
-------- -------- --------
fred          .00    23.40 
bert         1.00      .56 
charlie      2.00      .   
dick         3.00   -34.09 

4.1 DISPLAY.  
+--------+-------------------------------------------+--------+
|Variable|Description                                |Position|
#========#===========================================#========#
|vone    |Format: F8.2                               |       1|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|vtwo    |Format: F8.2                               |       2|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|vthree  |Format: A8                                 |       3|
|        |Measure: Nominal                           |        |
|        |Display Alignment: Left                    |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+
|v4      |Format: F8.2                               |       4|
|        |Measure: Scale                             |        |
|        |Display Alignment: Right                   |        |
|        |Display Width: 8                           |        |
+--------+-------------------------------------------+--------+

    vone     vtwo   vthree       v4
-------- -------- -------- --------
    1.00     3.00              5.00 
    2.00     4.00              6.00 

EOF
if [ $? -ne 0 ] ; then fail ; fi


activity="create program 2"
cat > $TESTFILE <<EOF
* This sheet is empty
GET DATA /TYPE=gnm /FILE='$TEMPDIR/Book1.gnumeric' /SHEET=name 'Empty'.


* This sheet doesnt exist
GET DATA /TYPE=gnm /FILE='$TEMPDIR/Book1.gnumeric' /SHEET=name 'foobarxx'.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 2"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output 2"
diff $TEMPDIR/pspp.list - <<EOF
warning: Selected sheet or range of spreadsheet "$TEMPDIR/Book1.gnumeric" is empty.
warning: Selected sheet or range of spreadsheet "$TEMPDIR/Book1.gnumeric" is empty.

EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
