#!/bin/sh

# This program tests the examples for GET DATA/TYPE=TXT given in the
# PSPP manual.

TEMPDIR=/tmp/pspp-tst-$$

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp

# ensure that top_srcdir is absolute
top_srcdir=`cd $top_srcdir; pwd`

STAT_CONFIG_PATH=$top_srcdir/config
export STAT_CONFIG_PATH

LANG=C
export LANG

cleanup()
{
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

activity="create passwd.data"
cat > passwd.data <<'EOF'
root:$1$nyeSP5gD$pDq/:0:0:,,,:/root:/bin/bash
blp:$1$BrP/pFg4$g7OG:1000:1000:Ben Pfaff,,,:/home/blp:/bin/bash
john:$1$JBuq/Fioq$g4A:1001:1001:John Darrington,,,:/home/john:/bin/bash
jhs:$1$D3li4hPL$88X1:1002:1002:Jason Stover,,,:/home/jhs:/bin/csh
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create cars.data"
cat > cars.data <<'EOF'
model   year    mileage price   type    age
Civic   2002    29883   15900   Si      2
Civic   2003    13415   15900   EX      1
Civic   1992    107000  3800    n/a     12
Accord  2002    26613   17900   EX      1
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create pets.data"
cat > pets.data <<'EOF'
"Pet Name", "Age", "Color", "Date Received", "Price", "Needs Walking", "Type"
, (Years), , , (Dollars), ,
"Rover", 4.5, Brown, "12 Feb 2004", 80, True, "Dog"
"Charlie", , Gold, "5 Apr 2007", 12.3, False, "Fish"
"Molly", 2, Black, "12 Dec 2006", 25, False, "Cat"
"Gilly", , White, "10 Apr 2007", 10, False, "Guinea Pig"
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="create test.pspp"
cat > test.pspp <<'EOF'
GET DATA /TYPE=TXT /FILE='passwd.data' /DELIMITERS=':'
        /VARIABLES=username A20
                   password A40
                   uid F10
                   gid F10
                   gecos A40
                   home A40
                   shell A40.
LIST.

GET DATA /TYPE=TXT /FILE='cars.data' /DELIMITERS=' ' /FIRSTCASE=2
        /VARIABLES=model A8
                   year F4
                   mileage F6
                   price F5
                   type A4
                   age F2.
LIST.

GET DATA /TYPE=TXT /FILE='cars.data' /ARRANGEMENT=FIXED /FIRSTCASE=2
        /VARIABLES=model 0-7 A
                   year 8-15 F
                   mileage 16-23 F
                   price 24-31 F
                   type 32-39 A
                   age 40-47 F.
LIST.

GET DATA /TYPE=TXT /FILE='pets.data' /DELIMITERS=', ' /QUALIFIER='"'
        /FIRSTCASE=3
        /VARIABLES=name A10
                   age F3.1
                   color A5
                   received EDATE10
                   price F5.2
                   needs_walking a5
                   type a10.
LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run test"
$SUPERVISOR $PSPP --testing-mode test.pspp
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare test results"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - <<'EOF'
            username                                 password        uid        gid                                    gecos                                     home                                    shell
-------------------- ---------------------------------------- ---------- ---------- ---------------------------------------- ---------------------------------------- ----------------------------------------
root                 $1$nyeSP5gD$pDq/                                  0          0 ,,,                                      /root                                    /bin/bash
blp                  $1$BrP/pFg4$g7OG                               1000       1000 Ben Pfaff,,,                             /home/blp                                /bin/bash
john                 $1$JBuq/Fioq$g4A                               1001       1001 John Darrington,,,                       /home/john                               /bin/bash
jhs                  $1$D3li4hPL$88X1                               1002       1002 Jason Stover,,,                          /home/jhs                                /bin/csh
   model year mileage price type age
-------- ---- ------- ----- ---- ---
Civic    2002   29883 15900 Si     2
Civic    2003   13415 15900 EX     1
Civic    1992  107000  3800 n/a   12
Accord   2002   26613 17900 EX     1
   model     year  mileage    price     type      age
-------- -------- -------- -------- -------- --------
Civic        2002    29883    15900 Si              2
Civic        2003    13415    15900 EX              1
Civic        1992   107000     3800 n/a            12
Accord       2002    26613    17900 EX              1
      name  age color   received  price needs_walking       type
---------- ---- ----- ---------- ------ ------------- ----------
Rover       4.5 Brown 12.02.2004  80.00         True  Dog
Charlie      .  Gold  05.04.2007  12.30         False Fish
Molly       2.0 Black 12.12.2006  25.00         False Cat
Gilly        .  White 10.04.2007  10.00         False Guinea Pig
EOF
if [ $? -ne 0 ] ; then fail ; fi



pass
