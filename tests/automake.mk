## Process this file with automake to produce Makefile.in  -*- makefile -*-

TESTS_ENVIRONMENT = top_srcdir='$(top_srcdir)' top_builddir='$(top_builddir)'
TESTS_ENVIRONMENT += PERL='@PERL@'
dist_TESTS = \
	tests/command/aggregate.sh \
	tests/command/autorecod.sh \
	tests/command/beg-data.sh \
	tests/command/bignum.sh \
	tests/command/count.sh \
	tests/command/datasheet.sh \
	tests/command/data-list.sh \
	tests/command/do-repeat.sh \
	tests/command/erase.sh \
	tests/command/examine.sh \
	tests/command/examine-extremes.sh \
	tests/command/examine-percentiles.sh \
	tests/command/file-label.sh \
	tests/command/file-handle.sh \
	tests/command/filter.sh \
	tests/command/flip.sh \
	tests/command/import-export.sh \
	tests/command/insert.sh \
	tests/command/lag.sh \
	tests/command/list.sh \
	tests/command/loop.sh \
	tests/command/longvars.sh \
	tests/command/match-files.sh \
	tests/command/missing-values.sh \
	tests/command/no_case_size.sh \
	tests/command/n_of_cases.sh \
	tests/command/npar-binomial.sh \
	tests/command/npar-chisquare.sh \
	tests/command/oneway.sh \
	tests/command/oneway-missing.sh \
	tests/command/oneway-with-splits.sh \
	tests/command/permissions.sh \
	tests/command/print.sh \
	tests/command/print-strings.sh \
	tests/command/rank.sh \
	tests/command/rename.sh \
	tests/command/regression.sh \
	tests/command/sample.sh \
	tests/command/sort.sh \
	tests/command/sysfiles.sh \
	tests/command/sysfiles-old.sh \
	tests/command/sysfile-info.sh \
	tests/command/split-file.sh \
	tests/command/t-test-1-indep-val.sh \
	tests/command/t-test-1-sample-missing-anal.sh \
	tests/command/t-test-1-sample-missing-list.sh \
	tests/command/t-test-1s.sh \
	tests/command/t-test-groups.sh \
	tests/command/t-test-indep-missing-anal.sh \
	tests/command/t-test-indep-missing-list.sh \
	tests/command/t-test-paired-missing-anal.sh \
	tests/command/t-test-paired-missing-list.sh \
	tests/command/t-test-pairs.sh \
	tests/command/trimmed-mean.sh \
	tests/command/tabs.sh \
	tests/command/use.sh \
	tests/command/vector.sh \
	tests/command/very-long-strings.sh \
	tests/command/weight.sh \
	tests/formats/bcd-in.sh \
	tests/formats/binhex-out.sh \
	tests/formats/date-in.sh \
	tests/formats/date-out.sh \
	tests/formats/float-format.sh \
	tests/formats/ib-in.sh \
	tests/formats/legacy-in.sh \
	tests/formats/month-in.sh \
	tests/formats/month-out.sh \
	tests/formats/num-in.sh \
	tests/formats/num-out.sh \
	tests/formats/time-in.sh \
	tests/formats/time-out.sh \
	tests/formats/wkday-in.sh \
	tests/formats/wkday-out.sh \
	tests/bugs/agg_crash.sh \
	tests/bugs/agg-crash-2.sh \
	tests/bugs/alpha-freq.sh \
	tests/bugs/big-input.sh \
	tests/bugs/big-input-2.sh \
	tests/bugs/case-map.sh \
	tests/bugs/comment-at-eof.sh \
	tests/bugs/compute-fmt.sh \
	tests/bugs/compression.sh \
	tests/bugs/crosstabs.sh \
	tests/bugs/crosstabs-crash.sh \
	tests/bugs/curtailed.sh \
	tests/bugs/data-crash.sh \
	tests/bugs/double-frequency.sh \
	tests/bugs/empty-do-repeat.sh \
	tests/bugs/get.sh \
	tests/bugs/examine-1sample.sh \
	tests/bugs/examine-missing.sh \
	tests/bugs/freq-nolabels.sh \
	tests/bugs/get-no-file.sh \
	tests/bugs/html-frequency.sh \
	tests/bugs/if_crash.sh \
	tests/bugs/input-crash.sh \
	tests/bugs/lag_crash.sh \
	tests/bugs/list-overflow.sh \
	tests/bugs/match-files-scratch.sh \
	tests/bugs/multipass.sh \
	tests/bugs/random.sh \
	tests/bugs/signals.sh \
	tests/bugs/t-test-with-temp.sh \
	tests/bugs/t-test.sh \
	tests/bugs/t-test-alpha.sh \
	tests/bugs/t-test-alpha2.sh \
	tests/bugs/temporary.sh \
	tests/bugs/val-labs.sh \
	tests/bugs/val-labs-trailing-slash.sh \
	tests/bugs/recode-copy-bug.sh \
	tests/bugs/computebug.sh \
	tests/bugs/compute-lv.sh \
	tests/bugs/compute-sum.sh \
	tests/bugs/temp-freq.sh \
	tests/bugs/print-crash.sh \
	tests/bugs/keep-all.sh \
	tests/xforms/recode.sh \
	tests/stats/descript-basic.sh \
	tests/stats/descript-missing.sh \
	tests/stats/descript-mean-bug.sh \
	tests/stats/moments.sh \
	tests/stats/percentiles-compatible.sh \
	tests/stats/ntiles.sh \
	tests/stats/percentiles-enhanced.sh \
	tests/expressions/expressions.sh \
	tests/expressions/epoch.sh \
	tests/expressions/randist.sh \
	tests/expressions/valuelabel.sh \
	tests/expressions/variables.sh \
	tests/expressions/vectors.sh

nodist_TESTS = \
	tests/libpspp/abt-test \
	tests/libpspp/bt-test \
	tests/libpspp/heap-test \
	tests/libpspp/ll-test \
	tests/libpspp/llx-test \
	tests/libpspp/range-map-test \
	tests/libpspp/range-set-test \
	tests/libpspp/sparse-array-test \
	tests/libpspp/tower-test

TESTS = $(dist_TESTS) $(nodist_TESTS)

check_PROGRAMS += \
	$(nodist_TESTS) \
	tests/formats/inexactify

tests_libpspp_ll_test_SOURCES = \
	src/libpspp/ll.c \
	src/libpspp/ll.h \
	tests/libpspp/ll-test.c

tests_libpspp_llx_test_SOURCES = \
	src/libpspp/ll.c \
	src/libpspp/ll.h \
	src/libpspp/llx.c \
	src/libpspp/llx.h \
	tests/libpspp/llx-test.c

tests_libpspp_heap_test_SOURCES = \
	src/libpspp/heap.c \
	src/libpspp/heap.h \
	src/libpspp/pool.c \
	src/libpspp/pool.h \
	tests/libpspp/heap-test.c
tests_libpspp_heap_test_LDADD = gl/libgl.la @LIBINTL@
tests_libpspp_heap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_abt_test_SOURCES = \
	src/libpspp/abt.c \
	src/libpspp/abt.h \
	tests/libpspp/abt-test.c
tests_libpspp_abt_test_LDADD = gl/libgl.la @LIBINTL@
tests_libpspp_abt_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_bt_test_SOURCES = \
	src/libpspp/bt.c \
	src/libpspp/bt.h \
	tests/libpspp/bt-test.c
tests_libpspp_bt_test_LDADD = gl/libgl.la
tests_libpspp_bt_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_range_map_test_SOURCES = \
	src/libpspp/bt.c \
	src/libpspp/bt.h \
	src/libpspp/range-map.c \
	src/libpspp/range-map.h \
	tests/libpspp/range-map-test.c
tests_libpspp_range_map_test_LDADD = gl/libgl.la @LIBINTL@
tests_libpspp_range_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_range_set_test_SOURCES = \
	src/libpspp/bt.c \
	src/libpspp/bt.h \
	src/libpspp/pool.c \
	src/libpspp/pool.h \
	src/libpspp/range-set.c \
	src/libpspp/range-set.h \
	tests/libpspp/range-set-test.c
tests_libpspp_range_set_test_LDADD = gl/libgl.la @LIBINTL@
tests_libpspp_range_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_tower_test_SOURCES = \
	src/libpspp/abt.c \
	src/libpspp/abt.h \
	src/libpspp/pool.c \
	src/libpspp/pool.h \
	src/libpspp/tower.c \
	src/libpspp/tower.h \
	tests/libpspp/tower-test.c
tests_libpspp_tower_test_LDADD = gl/libgl.la @LIBINTL@
tests_libpspp_tower_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_sparse_array_test_SOURCES = \
	src/libpspp/sparse-array.c \
	src/libpspp/sparse-array.h \
	src/libpspp/pool.c \
	src/libpspp/pool.h \
	tests/libpspp/sparse-array-test.c
tests_libpspp_sparse_array_test_LDADD = gl/libgl.la @LIBINTL@
tests_libpspp_sparse_array_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_formats_inexactify_SOURCES = tests/formats/inexactify.c

EXTRA_DIST += \
	$(dist_TESTS) \
	tests/weighting.data tests/data-list.data tests/list.data \
	tests/no_case_size.sav \
	tests/coverage.sh tests/test_template \
	tests/v13.sav tests/v14.sav \
	tests/bugs/computebug.stat tests/bugs/computebug.out \
	tests/bugs/recode-copy-bug-1.stat tests/bugs/recode-copy-bug-2.stat \
	tests/bugs/recode-copy-bug-1.out tests/bugs/recode-copy-bug-2.out \
	tests/expressions/randist/beta.out \
	tests/expressions/randist/cauchy.out \
	tests/expressions/randist/chisq.out \
	tests/expressions/randist/exp.out \
	tests/expressions/randist/f.out \
	tests/expressions/randist/gamma.out \
	tests/expressions/randist/laplace.out \
	tests/expressions/randist/lnormal.out \
	tests/expressions/randist/logistic.out \
	tests/expressions/randist/normal.out \
	tests/expressions/randist/pareto.out \
	tests/expressions/randist/compare.pl \
	tests/expressions/randist/randist.pl \
	tests/expressions/randist/randist.txt \
	tests/expressions/randist/t.out \
	tests/expressions/randist/uniform.out \
	tests/expressions/randist/weibull.out \
	tests/formats/bcd-in.expected.cmp.gz \
	tests/formats/binhex-out.expected.gz \
	tests/formats/ib-in.expected.cmp.gz \
	tests/formats/legacy-in.expected.cmp.gz \
	tests/formats/num-in.expected.gz \
	tests/formats/num-out.expected.cmp.gz \
	tests/formats/num-out-cmp.pl \
	tests/formats/num-out-compare.pl \
	tests/formats/num-out-decmp.pl \
	tests/formats/num-out.pl

CLEANFILES += *.save pspp.* foo*

