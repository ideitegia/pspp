#!/bin/sh

# This program tests the IMPORTCASES feature of GET DATA /TYPE=TXT.

TEMPDIR=/tmp/pspp-tst-$$
TESTFILE=$TEMPDIR/`basename $0`.sps
: ${PERL:=perl}

# ensure that top_builddir  are absolute
if [ -z "$top_builddir" ] ; then top_builddir=. ; fi
if [ -z "$top_srcdir" ] ; then top_srcdir=. ; fi
top_builddir=`cd $top_builddir; pwd`
PSPP=$top_builddir/src/ui/terminal/pspp$EXEEXT

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

activity="create data file using Perl"
$PERL > test.data <<'EOF'
for ($i = 1; $i <= 100; $i++) {
    printf "%02d\n", $i;
}
EOF
if [ $? -ne 0 ] ; then no_result ; fi

# Create command file.
activity="create program"
cat > $TESTFILE << EOF
get data /type=txt /file='test.data' /importcases=first 10 /variables x f8.0.
list.

get data /type=txt /file='test.data' /importcases=percent 1 /variables x f8.0.
list.

get data /type=txt /file='test.data' /importcases=percent 35 /variables x f8.0.
list.

get data /type=txt /file='test.data' /importcases=percent 95 /variables x f8.0.
list.

get data /type=txt /file='test.data' /importcases=percent 100 /variables x f8.0.
list.
EOF
if [ $? -ne 0 ] ; then no_result ; fi

activity="run program"
$SUPERVISOR $PSPP -o pspp.csv $TESTFILE
if [ $? -ne 0 ] ; then fail ; fi

activity="compare output"
diff -c $TEMPDIR/pspp.csv - << EOF
Table: Data List
x
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

Table: Data List
x
1
2

Table: Data List
x
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
14
15
16
17
18
19
20
21
22
23
24
25
26
27
28
29
30
31
32
33
34
35
36

Table: Data List
x
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
14
15
16
17
18
19
20
21
22
23
24
25
26
27
28
29
30
31
32
33
34
35
36
37
38
39
40
41
42
43
44
45
46
47
48
49
50
51
52
53
54
55
56
57
58
59
60
61
62
63
64
65
66
67
68
69
70
71
72
73
74
75
76
77
78
79
80
81
82
83
84
85
86
87
88
89
90
91
92
93
94
95
96

Table: Data List
x
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
14
15
16
17
18
19
20
21
22
23
24
25
26
27
28
29
30
31
32
33
34
35
36
37
38
39
40
41
42
43
44
45
46
47
48
49
50
51
52
53
54
55
56
57
58
59
60
61
62
63
64
65
66
67
68
69
70
71
72
73
74
75
76
77
78
79
80
81
82
83
84
85
86
87
88
89
90
91
92
93
94
95
96
97
98
99
100
EOF
if [ $? -ne 0 ] ; then fail ; fi

pass;
