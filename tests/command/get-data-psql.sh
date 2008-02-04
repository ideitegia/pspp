#!/bin/sh

# This program tests the psql import feature.

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

port=6543
dbase=pspptest
PG_CONFIG=pg_config
pgpath=`$PG_CONFIG | awk '/BINDIR/{print $3}'`

cleanup()
{
    if [ x"$PSPP_TEST_NO_CLEANUP" != x ] ; then 
	echo "NOT cleaning $TEMPDIR"
     	return ; 
    fi
    PGHOST=$TEMPDIR $pgpath/pg_ctl -D $TEMPDIR/cluster  stop -w -o "-k $TEMPDIR -h ''"   > /dev/null 2>&1
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

if [ ! -x $pgpath/initdb ] ; then
  echo 'No Postgres server was found, so the postgres database interface cannot be tested.'  
  cleanup;
  exit 77;
fi

mkdir -p $TEMPDIR

cd $TEMPDIR

activity="create cluster"
$pgpath/initdb  -D $TEMPDIR/cluster -A trust > /dev/null
if [ $? -ne 0 ] ; then no_result ; fi


activity="run server"
PGHOST=$TEMPDIR $pgpath/pg_ctl -D $TEMPDIR/cluster  start -w -o "-k $TEMPDIR -p $port -h ''"  > /dev/null
if [ $? -ne 0 ] ; then no_result ; fi


activity="create database"
createdb  -h $TEMPDIR  -p $port $dbase > /dev/null 2> /dev/null


activity="populate database"
psql  -h $TEMPDIR -p $port  $dbase > /dev/null << EOF
CREATE TABLE thing (
 bool    bool                      ,
 bytea   bytea                     ,
 char    char                      ,
 int8    int8                      ,
 int2    int2                      ,
 int4    int4                      ,
 numeric       numeric(50,6)       ,
 text    text                      ,
 oid     oid                       ,
 float4  float4                    ,
 float8  float8                    ,
 money   money                     ,
 pbchar  bpchar                    ,
 varchar varchar                   ,
 date    date                      ,
 time    time                      ,
 timestamp     timestamp           ,
 timestamptz   timestamptz         ,
 interval      interval            ,
 timetz        timetz              
);

INSERT INTO thing VALUES (
 false,
 '0',
 'a',
 '0',
 0,
 0,
 -256.098,
 'this-long-text',
 0,
 0,
 0,
 '0.01',
 'a',
 'A',
 '1-Jan-2000',
 '00:00',
 'January 8 04:05:06 1999',
 'January 8 04:05:06 1999 PST',
 '1 minutes',
 '10:09 UTC+4'
);

INSERT INTO thing VALUES (
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null,
 null
);

INSERT INTO thing VALUES (
 true,
 '1',
 'b',
 '1',
 1,
 1,
 65535.00001,
 'that-long-text',
 1,
 1,
 1,
 '1.23',
 'b',
 'B',
 '10-Jan-1963',
 '01:05:02',
 '10-Jan-1963 23:58:00',
 '10-Jan-1963 23:58:00 CET',
 '2 year 1 month 12 days 1 hours 3 minutes 4 seconds',
 '01:05:02 UTC-7'
);
EOF
if [ $? -ne 0 ] ; then fail ; fi

activity="create program"
cat > $TESTFILE <<EOF
GET DATA /TYPE=psql 
	/CONNECT="host=$TEMPDIR port=$port dbname=$dbase"
	/UNENCRYPTED
	/SQL="select * from thing".

DISPLAY DICTIONARY.

LIST.
EOF
if [ $? -ne 0 ] ; then no_result ; fi


activity="run program"
$SUPERVISOR $PSPP --testing-mode -o raw-ascii $TESTFILE
if [ $? -ne 0 ] ; then no_result ; fi

activity="compare output"
perl -pi -e 's/^\s*$//g' $TEMPDIR/pspp.list
diff -b  $TEMPDIR/pspp.list - << 'EOF'
1.1 DISPLAY.  
+---------------+-------------------------------------------+--------+
|Variable       |Description                                |Position|
#===============#===========================================#========#
|bool           |Format: F8.2                               |       1|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|bytea          |Format: AHEX2                              |       2|
|               |Measure: Nominal                           |        |
|               |Display Alignment: Left                    |        |
|               |Display Width: 1                           |        |
+---------------+-------------------------------------------+--------+
|char           |Format: A8                                 |       3|
|               |Measure: Nominal                           |        |
|               |Display Alignment: Left                    |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|int8           |Format: F8.2                               |       4|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|int2           |Format: F8.2                               |       5|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|int4           |Format: F8.2                               |       6|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|numeric        |Format: E40.6                              |       7|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|text           |Format: A16                                |       8|
|               |Measure: Nominal                           |        |
|               |Display Alignment: Left                    |        |
|               |Display Width: 16                          |        |
+---------------+-------------------------------------------+--------+
|oid            |Format: F8.2                               |       9|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|float4         |Format: F8.2                               |      10|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|float8         |Format: F8.2                               |      11|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|money          |Format: DOLLAR8.2                          |      12|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|pbchar         |Format: A8                                 |      13|
|               |Measure: Nominal                           |        |
|               |Display Alignment: Left                    |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|varchar        |Format: A8                                 |      14|
|               |Measure: Nominal                           |        |
|               |Display Alignment: Left                    |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|date           |Format: DATE11                             |      15|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|time           |Format: TIME11.0                           |      16|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|timestamp      |Format: DATETIME22.0                       |      17|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|timestamptz    |Format: DATETIME22.0                       |      18|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|interval       |Format: DTIME13.0                          |      19|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|interval_months|Format: F3.0                               |      20|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|timetz         |Format: TIME11.0                           |      21|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
|timetz_zone    |Format: F8.2                               |      22|
|               |Measure: Scale                             |        |
|               |Display Alignment: Right                   |        |
|               |Display Width: 8                           |        |
+---------------+-------------------------------------------+--------+
    bool bytea     char     int8     int2     int4                                  numeric             text      oid   float4   float8    money   pbchar  varchar        date        time              timestamp            timestamptz      interval interval_months      timetz timetz_zone
-------- ----- -------- -------- -------- -------- ---------------------------------------- ---------------- -------- -------- -------- -------- -------- -------- ----------- ----------- ---------------------- ---------------------- ------------- --------------- ----------- -----------
     .00    30 a             .00      .00      .00                           -2.560980E+002 this-long-text        .00      .00      .00     $.01 a        A        01-JAN-2000     0:00:00   08-JAN-1999 04:05:06   08-JAN-1999 12:05:06    0 00:01:00               0    10:09:00        4.00 
     .      20               .        .        .                                .                                 .        .        .        .                               .           .                      .                      .             .               .           .         .   
    1.00    31 b            1.00     1.00     1.00                            6.553500E+004 that-long-text        .00     1.00     1.00    $1.23 b        B        10-JAN-1963     1:05:02   10-JAN-1963 23:58:00   10-JAN-1963 22:58:00   12 01:03:04              25     1:05:02       -7.00 
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
