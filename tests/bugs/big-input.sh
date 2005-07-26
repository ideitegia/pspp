#!/bin/sh

# This program tests for a bug which caused a crash when 
# a large number of cases where presented.


TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps

here=`pwd`;

# ensure that top_srcdir is absolute
cd $top_srcdir; top_srcdir=`pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH


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
cat > $TEMPDIR/foo.sps <<EOF
INPUT PROGRAM.
	LOOP #I=1 TO 50000.
		COMPUTE X=NORMAL(10).
		END CASE.
	END LOOP.
	END FILE.
END INPUT PROGRAM.


EXAMINE /x
	/STATISTICS=DESCRIPTIVES.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $here/../src/pspp -o raw-ascii $TEMPDIR/foo.sps > /dev/null
if [ $? -ne 0 ] ; then fail ; fi

pass;
