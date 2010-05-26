#! /bin/sh

TEMPDIR=/tmp/pspp-tst-$$
mkdir -p $TEMPDIR
trap 'cd /; rm -rf $TEMPDIR' 0

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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
cat > month-in.pspp <<EOF
set errors=none.
set mxwarns=10000000.
data list /month3 1-3 (month)
           month4 1-4 (month)
           month5 1-5 (month)
           month6 1-6 (month)
           month7 1-7 (month)
           month8 1-8 (month)
           month9 1-9 (month)
           month10 1-10 (month).
begin data.

.
i
ii
iii
iiii
iv
v
vi
vii
viii
ix
viiii
x
xi
xii
0
1
2
3
4
5
6
7
8
9
10
11
12
13
january
janaury
february
febraury
march
marhc
april
may
june
july
august
september
october
november
decmeber
december
end data.
formats all (month3).
print outfile='month-in.out'/all.
execute.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv month-in.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
diff -u month-in.out - <<EOF
   .   .   .   .   .   .   .   . 
   .   .   .   .   .   .   .   . 
 JAN JAN JAN JAN JAN JAN JAN JAN 
 FEB FEB FEB FEB FEB FEB FEB FEB 
 MAR MAR MAR MAR MAR MAR MAR MAR 
 MAR   .   .   .   .   .   .   . 
 APR APR APR APR APR APR APR APR 
 MAY MAY MAY MAY MAY MAY MAY MAY 
 JUN JUN JUN JUN JUN JUN JUN JUN 
 JUL JUL JUL JUL JUL JUL JUL JUL 
 JUL AUG AUG AUG AUG AUG AUG AUG 
 SEP SEP SEP SEP SEP SEP SEP SEP 
 JUL AUG AUG AUG AUG AUG AUG AUG 
 OCT OCT OCT OCT OCT OCT OCT OCT 
 NOV NOV NOV NOV NOV NOV NOV NOV 
 DEC DEC DEC DEC DEC DEC DEC DEC 
   .   .   .   .   .   .   .   . 
 JAN JAN JAN JAN JAN JAN JAN JAN 
 FEB FEB FEB FEB FEB FEB FEB FEB 
 MAR MAR MAR MAR MAR MAR MAR MAR 
 APR APR APR APR APR APR APR APR 
 MAY MAY MAY MAY MAY MAY MAY MAY 
 JUN JUN JUN JUN JUN JUN JUN JUN 
 JUL JUL JUL JUL JUL JUL JUL JUL 
 AUG AUG AUG AUG AUG AUG AUG AUG 
 SEP SEP SEP SEP SEP SEP SEP SEP 
 OCT OCT OCT OCT OCT OCT OCT OCT 
 NOV NOV NOV NOV NOV NOV NOV NOV 
 DEC DEC DEC DEC DEC DEC DEC DEC 
   .   .   .   .   .   .   .   . 
 JAN JAN JAN JAN JAN JAN JAN JAN 
 JAN JAN JAN JAN JAN JAN JAN JAN 
 FEB FEB FEB FEB FEB FEB FEB FEB 
 FEB FEB FEB FEB FEB FEB FEB FEB 
 MAR MAR MAR MAR MAR MAR MAR MAR 
 MAR MAR MAR MAR MAR MAR MAR MAR 
 APR APR APR APR APR APR APR APR 
 MAY MAY MAY MAY MAY MAY MAY MAY 
 JUN JUN JUN JUN JUN JUN JUN JUN 
 JUL JUL JUL JUL JUL JUL JUL JUL 
 AUG AUG AUG AUG AUG AUG AUG AUG 
 SEP SEP SEP SEP SEP SEP SEP SEP 
 OCT OCT OCT OCT OCT OCT OCT OCT 
 NOV NOV NOV NOV NOV NOV NOV NOV 
 DEC DEC DEC DEC DEC DEC DEC DEC 
 DEC DEC DEC DEC DEC DEC DEC DEC 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass
