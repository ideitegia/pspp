#! /bin/sh

TEMPDIR=/tmp/pspp-tst-$$
mkdir -p $TEMPDIR
trap 'cd /; rm -rf $TEMPDIR' 0

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH

fail()
{
    echo $activity
    echo FAILED
    exit 1;
}


no_result()
{
    echo $activity
    echo NO RESULT;
    exit 2;
}

pass()
{
    exit 0;
}

cd $TEMPDIR

activity="write pspp syntax"
cat > binhex-out.pspp <<EOF
set errors=none.
set mxwarns=10000000.
set wib=msbfirst.
data list /x 1-10.
begin data.

2
11
123
1234
913
3.14159
777
82
690
-2
-11
-123
-1234
-913
-3.14159
-777
-82
-690
-.1
-.5
-.9
9999.1
9999.5
9999.9
10000
18231237
-9999.1
-9999.5
-9999.9
-10000
-8231237
999.1
999.5
999.9
1000
8231237
-999.1
-999.5
-999.9
-1000
-8231237
99.1
99.5
99.9
100
821237
-99.1
-99.5
-99.9
-100
-831237
9.1
9.5
9.9
10
81237
-9.1
-9.5
-9.9
-10
-81237
1.1
-1.1
1.5
-1.5
1.9
-1.9
end data.
file handle output/name='binhex.out'/mode=image/lrecl=256.
write outfile=output/
	x(p1.0) x(p2.0) x(p3.0) x(p4.0)		/* 000
	x(p2.1) x(p3.1) x(p4.1)			/* 00a
	x(p3.2) x(p4.2)				/* 013
	x(p4.3)					/* 01a
	x(pk1.0) x(pk2.0) x(pk3.0) x(pk4.0)	/* 01e
	x(pk2.1) x(pk3.1) x(pk4.1)		/* 028
	x(pk3.2) x(pk4.2)			/* 031
	x(pk4.3)				/* 038
	x(ib1.0) x(ib2.0) x(ib3.0) x(ib4.0)	/* 03c
	x(ib1.1) x(ib2.1) x(ib3.1) x(ib4.1)	/* 046
	x(ib1.2) x(ib2.2) x(ib3.2) x(ib4.2)	/* 050
	x(ib1.3) x(ib2.3) x(ib3.3) x(ib4.3)	/* 05a
	x(ib2.4) x(ib3.4) x(ib4.4)		/* 064
	x(ib2.5) x(ib3.5) x(ib4.5)		/* 06d
	x(ib3.6) x(ib4.6)			/* 076
	x(ib3.7) x(ib4.7)			/* 07d
	x(ib3.8) x(ib4.8)			/* 084
	x(ib4.9)				/* 08b
	x(ib4.10)				/* 08f
	x(pib1.0) x(pib2.0) x(pib3.0) x(pib4.0)	/* 093
	x(pib1.1) x(pib2.1) x(pib3.1) x(pib4.1)	/* 09d
	x(pib1.2) x(pib2.2) x(pib3.2) x(pib4.2)	/* 0a7
	x(pib1.3) x(pib2.3) x(pib3.3) x(pib4.3)	/* 0b1
	x(pib2.4) x(pib3.4) x(pib4.4)		/* 0bb
	x(pib2.5) x(pib3.5) x(pib4.5)		/* 0c4
	x(pib3.6) x(pib4.6)			/* 0cd
	x(pib3.7) x(pib4.7)			/* 0d4
	x(pib3.8) x(pib4.8)			/* 0db
	x(pib4.9)				/* 0e2
	x(pib4.10)				/* 0e6
	x(pibhex2) x(pibhex4)			/* 0ea
        x(pibhex6) x(pibhex8).			/* 0f0
						/* 0fe
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv binhex-out.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="gunzip expected results"
gzip -cd < $top_srcdir/tests/formats/binhex-out.expected.gz > expected.out
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
cmp expected.out binhex.out
if [ $? -ne 0 ] ; then fail ; fi

pass
