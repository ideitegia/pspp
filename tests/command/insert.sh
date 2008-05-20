#!/bin/sh

# This program tests the INSERT command

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

activity="create wrapper 1"
cat <<EOF > $TESTFILE
INSERT 
  FILE='$TEMPDIR/foo.sps'
  SYNTAX=INTERACTIVE
  .


LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

#The following syntax intentionally omits periods from some lines
#It's an example of "batch" syntax
activity="create insert"
cat <<EOF > $TEMPDIR/foo.sps
input program.
+  loop #i = 1 to 100.
+    compute z = #i
+    end case.
+  end loop
end file.
end input program.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


#This command should fail
activity="run program 1"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE > /dev/null
if [ $? -eq 0 ] ; then fail ; fi


activity="create wrapper 2"
cat <<EOF > $TESTFILE
INSERT 
  FILE='$TEMPDIR/foo.sps'
  SYNTAX=BATCH
  .


LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 2"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi


# Now test the CD subcommand

activity="mkdir 1"
mkdir $TEMPDIR/Dir1
if [ $? -ne 0 ] ; then no_result ; fi

activity="create wrapper 3"
cat <<EOF > $TESTFILE
INSERT 
  FILE='$TEMPDIR/Dir1/foo.sps'
  CD=NO
  .


LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create wrapper 4"
cat <<EOF > $TEMPDIR/Dir1/foo.sps
INSERT 
  FILE='bar.sps'
  CD=NO
  .

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create wrapper 5"
cat <<EOF > $TEMPDIR/Dir1/bar.sps
DATA LIST LIST /x *.
BEGIN DATA.
1
2
3
END DATA.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


# This command should fail
activity="run program 3"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE > /dev/null
if [ $? -eq 0 ] ; then fail ; fi

activity="create wrapper 6"
cat <<EOF > $TESTFILE
INSERT 
  FILE='$TEMPDIR/Dir1/foo.sps'
  CD=YES
  .

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 4"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi


# Now test the ERROR= feature

activity="create wrapper 7"
cat <<EOF > $TESTFILE
INSERT 
  FILE='$TEMPDIR/foo.sps'
  ERROR=STOP.
  .

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="create included file"
cat <<EOF > $TEMPDIR/foo.sps
DATA LIST NOTABLE LIST /x *.
BEGIN DATA.
1
2
3
END DATA.

* The following line is erroneous

DISPLAY AKSDJ.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 5"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 1 ] ; then no_result ; fi

activity="examine output 1"
diff $TEMPDIR/pspp.list - <<EOF
$TEMPDIR/foo.sps:10: error: DISPLAY: AKSDJ is not a variable name.
warning: Error encountered while ERROR=STOP is effective.
$TEMPDIR/foo.sps:10: error: Stopping syntax file processing here to avoid a cascade of dependent command failures.

EOF
if [ $? -ne 0 ] ; then fail ; fi


activity="create wrapper 8"
cat <<EOF > $TESTFILE
INSERT 
  FILE='$TEMPDIR/foo.sps'
  ERROR=CONTINUE.
  .

LIST.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program 6"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE > /dev/null
if [ $? -ne 1 ] ; then no_result ; fi

activity="examine output 2"
diff $TEMPDIR/pspp.list - <<EOF
$TEMPDIR/foo.sps:10: error: DISPLAY: AKSDJ is not a variable name.

       x
--------
    1.00 
    2.00 
    3.00 

EOF
if [ $? -ne 0 ] ; then fail ; fi



pass;
