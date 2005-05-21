#!/bin/sh

# This program tests that system files can be read properly, even when the 
# case_size header value is -1 (Some 3rd party products do this)


TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


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

cat <<EOF > $TESTFILE
GET FILE='$top_srcdir/tests/no_case_size.sav'.
DISPLAY DICTIONARY.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -b -B -w pspp.list - <<EOF
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
Asia    
        
        
        

Africa  
        
        
        

North Am
erica   
        
        

South Am
erica   
        
        

Antarcti
ca      
        
        

Europe  
        
        
        

Australi
a/Oceani
a       
        



EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
