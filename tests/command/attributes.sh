#!/bin/sh

# This program tests VARIABLE ATTRIBUTE and DATAFILE ATTRIBUTE
# commands, including the ability to write attributes to system files
# and read them back in again.

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
cat > $TESTFILE <<EOF
DATA LIST FREE/a b c.
BEGIN DATA.
1 2 3
END DATA.

DATAFILE ATTRIBUTE
	ATTRIBUTE=key('value')
                  array('array element 1')
                  Array[2]('array element 2').
VARIABLE ATTRIBUTE
        VARIABLES=a b
        ATTRIBUTE=ValidationRule[2]("a + b > 2")
                  ValidationRule[1]('a * b > 3')
       /VARIABLES=c
        ATTRIBUTE=QuestionWording('X or Y?').
DISPLAY ATTRIBUTES.

SAVE OUTFILE='attributes.sav'.
NEW FILE.
GET FILE='attributes.sav'.

DATAFILE ATTRIBUTE
         DELETE=Array[1] Array[2].
VARIABLE ATTRIBUTE
         VARIABLES=a
         DELETE=ValidationRule
        /VARIABLE=b
         DELETE=validationrule[2].

DISPLAY ATTRIBUTES.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  -w $TEMPDIR/pspp.list - << EOF
1.1 DISPLAY.  
+--------+-----------------+-----------------------------------+
|Variable|Description      |                                   |
#========#=================#===================================#
|a       |Custom attributes|                                   |
|        |ValidationRule[1]|a * b > 3                          |
|        |ValidationRule[2]|a + b > 2                          |
+--------+-----------------+-----------------------------------+
|b       |Custom attributes|                                   |
|        |ValidationRule[1]|a * b > 3                          |
|        |ValidationRule[2]|a + b > 2                          |
+--------+-----------------+-----------------------------------+
|c       |Custom attributes|                                   |
|        |QuestionWording  |X or Y?                            |
+--------+-----------------+-----------------------------------+
1.2 DISPLAY.  Custom data file attributes.
+---------+---------------+
|Attribute|Value          |
#=========#===============#
|array[1] |array element 1|
|array[2] |array element 2|
|key      |value          |
+---------+---------------+
2.1 DISPLAY.  
+--------+---------------+-----------------------------------+
|Variable|Description    |                                   |
#========#===============#===================================#
|b       |Custom attribut|s:                                 |
|        |ValidationRule |a * b > 3                          |
+--------+---------------+-----------------------------------+
|c       |Custom attribut|s:                                 |
|        |QuestionWording|X or Y?                            |
+--------+---------------+-----------------------------------+
2.2 DISPLAY.  Custom data file attributes.
+---------+---------------+
|Attribute|Value          |
#=========#===============#
|array    |array element 2|
|key      |value          |
+---------+---------------+
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
