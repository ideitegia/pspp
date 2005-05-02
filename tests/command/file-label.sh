#!/bin/sh

# This program tests the FILE LABEL and  DOCUMENT commands

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

export STAT_CONFIG_PATH=$top_srcdir/config


cleanup()
{
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
cat > $TESTFILE << EOF

/* Set up a dummy active file in memory.
data list /X 1 Y 2.
begin data.
16
27
38
49
50
end data.

/* Add value labels for some further testing of value labels.
value labels x y 1 'first label' 2 'second label' 3 'third label'.
add value labels x 1 'first label mark two'.

/* Add a file label and a few documents.
file label This is a test file label.
document First line of a document
This is the second very long line of a document in an attempt to overflow the input buffer with a really long line
Note that the last line should end with a period: .

/* Display the documents.
display documents.
display file label.	/* undocumented feature of PSPP

/* Save the active file then get it and display the documents again.
save /OUTFILE='foo.save'.
get /FILE='foo.save'.
display documents.
display file label.	/* undocumented feature of PSPP

/* There is an interesting interaction that occurs if the 'execute'
/* command below.  What happens is that an error message is output
/* at the next 'save' command that 'foo.save' is already open for
/* input.  This is because the 'get' hasn't been executed yet and
/* therefore PSPP would be reading from and writing to the same
/* file at once, which is obviously a Bad Thing.  But 'execute'
/* here clears up that potential problem.
execute.

/* Add another (shorter) document and try again.
document There should be another document now.
display documents.

/* Save and get.
save /OUTFILE='foo.save'.
get /FILE='foo.save'.
display documents.
display file label.	/* undocumented feature of PSPP

/* Done.

EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

# We need to filter out the dates/times
activity="date filter"
grep -v 'Document entered' $TEMPDIR/pspp.list > $TEMPDIR/pspp.filtered
if [ $? -ne 0 ] ; then no_result ; fi


activity="compare results"
diff -b -B $TEMPDIR/pspp.filtered - <<EOF
1.1 DATA LIST.  Reading 1 record from the command file.
+--------+------+-------+------+
|Variable|Record|Columns|Format|
#========#======#=======#======#
|X       |     1|  1-  1|F1.0  |
|Y       |     1|  2-  2|F1.0  |
+--------+------+-------+------+

Documents in the active file:

   First line of a document
   This is the second very long line of a document in an attempt to overflow the
   Note that the last line should end with a period: .

File label:
This is a test file label

Documents in the active file:

   First line of a document
   This is the second very long line of a document in an attempt to overflow the
   Note that the last line should end with a period: .

File label:
This is a test file label

Documents in the active file:

   First line of a document
   This is the second very long line of a document in an attempt to overflow the
   Note that the last line should end with a period: .
 
   There should be another document now.

Documents in the active file:

   First line of a document
   This is the second very long line of a document in an attempt to overflow the
   Note that the last line should end with a period: .
 
   There should be another document now.

File label:
This is a test file label
EOF
if [ $? -ne 0 ] ; then fail ; fi


pass;
