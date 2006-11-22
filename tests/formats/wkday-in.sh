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
cat > wkday-in.pspp <<EOF
set errors=none.
set mxwarns=10000000.
data list /wkday2 1-2 (wkday)
	   wkday3 1-3 (wkday)
           wkday4 1-4 (wkday)
           wkday5 1-5 (wkday)
           wkday6 1-6 (wkday)
           wkday7 1-7 (wkday)
           wkday8 1-8 (wkday)
           wkday9 1-9 (wkday)
           wkday10 1-10 (wkday).
begin data.

.
monady
tuseday
wedensday
thurdsay
fridya
saturady
sudnay
end data.
formats all (wkday2).
print outfile='wkday-in.out'/all.
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP --testing-mode wkday-in.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -u wkday-in.out - <<EOF
  .  .  .  .  .  .  .  .  . 
  .  .  .  .  .  .  .  .  . 
 MO MO MO MO MO MO MO MO MO 
 TU TU TU TU TU TU TU TU TU 
 WE WE WE WE WE WE WE WE WE 
 TH TH TH TH TH TH TH TH TH 
 FR FR FR FR FR FR FR FR FR 
 SA SA SA SA SA SA SA SA SA 
 SU SU SU SU SU SU SU SU SU 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
