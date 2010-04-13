## Process this file with automake to produce Makefile.in  -*- makefile -*-

TESTS_ENVIRONMENT = top_srcdir='$(top_srcdir)' top_builddir='$(top_builddir)'
TESTS_ENVIRONMENT += PERL='$(PERL)' PG_CONFIG='$(PG_CONFIG)'

# Allow locale_charset to find charset.alias before running "make install".
TESTS_ENVIRONMENT += CHARSETALIASDIR='$(abs_top_builddir)/gl'

TESTS_ENVIRONMENT += LC_ALL=C

dist_TESTS = \
	tests/command/add-files.sh \
	tests/command/aggregate.sh \
	tests/command/attributes.sh \
	tests/command/beg-data.sh \
	tests/command/bignum.sh \
	tests/command/count.sh \
	tests/command/correlation.sh \
	tests/command/data-list.sh \
	tests/command/do-if.sh \
	tests/command/do-repeat.sh \
	tests/command/erase.sh \
	tests/command/examine.sh \
	tests/command/examine-extremes.sh \
	tests/command/examine-percentiles.sh \
	tests/command/file-label.sh \
	tests/command/file-handle.sh \
	tests/command/filter.sh \
	tests/command/flip.sh \
	tests/command/get-data-txt.sh \
	tests/command/get-data-txt-examples.sh \
	tests/command/get-data-txt-importcases.sh \
	tests/command/import-export.sh \
	tests/command/insert.sh \
	tests/command/lag.sh \
	tests/command/line-ends.sh \
	tests/command/list.sh \
	tests/command/loop.sh \
	tests/command/longvars.sh \
	tests/command/match-files.sh \
	tests/command/missing-values.sh \
	tests/command/no_case_size.sh \
	tests/command/n_of_cases.sh \
	tests/command/npar-binomial.sh \
	tests/command/npar-chisquare.sh \
	tests/command/npar-wilcoxon.sh \
	tests/command/npar-sign.sh \
	tests/command/oneway.sh \
	tests/command/oneway-missing.sh \
	tests/command/oneway-with-splits.sh \
	tests/command/permissions.sh \
	tests/command/print.sh \
	tests/command/print-strings.sh \
	tests/command/rank.sh \
	tests/command/rename.sh \
	tests/command/regression.sh \
	tests/command/regression-qr.sh \
	tests/command/reliability.sh \
	tests/command/roc.sh \
	tests/command/roc2.sh \
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
	tests/command/update.sh \
	tests/command/use.sh \
	tests/command/variable-display.sh \
	tests/command/vector.sh \
	tests/command/very-long-strings.sh \
	tests/command/weight.sh \
	tests/formats/bcd-in.sh \
	tests/formats/binhex-out.sh \
	tests/formats/date-in.sh \
	tests/formats/date-out.sh \
	tests/formats/float-format.sh \
	tests/formats/format-guesser.sh \
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
	tests/formats/360.sh \
	tests/bugs/agg_crash.sh \
	tests/bugs/agg-crash-2.sh \
	tests/bugs/big-input.sh \
	tests/bugs/big-input-2.sh \
	tests/bugs/case-map.sh \
	tests/bugs/comment-at-eof.sh \
	tests/bugs/compression.sh \
	tests/bugs/curtailed.sh \
	tests/bugs/data-crash.sh \
	tests/bugs/empty-do-repeat.sh \
	tests/bugs/get.sh \
	tests/bugs/examine-crash.sh \
	tests/bugs/examine-crash2.sh \
	tests/bugs/examine-crash3.sh \
	tests/bugs/examine-1sample.sh \
	tests/bugs/examine-missing.sh \
	tests/bugs/examine-missing2.sh \
	tests/bugs/get-no-file.sh \
	tests/bugs/if_crash.sh \
	tests/bugs/input-crash.sh \
	tests/bugs/lag_crash.sh \
	tests/bugs/list-overflow.sh \
	tests/bugs/match-files-scratch.sh \
	tests/bugs/multipass.sh \
	tests/bugs/overwrite-input-file.sh \
	tests/bugs/overwrite-special-file.sh \
	tests/bugs/random.sh \
	tests/bugs/shbang.sh \
	tests/bugs/signals.sh \
	tests/bugs/t-test-with-temp.sh \
	tests/bugs/t-test.sh \
	tests/bugs/t-test-alpha.sh \
	tests/bugs/t-test-alpha2.sh \
	tests/bugs/t-test-alpha3.sh \
	tests/bugs/t-test-paired.sh \
	tests/bugs/temporary.sh \
	tests/bugs/unwritable-dir.sh \
	tests/bugs/val-labs.sh \
	tests/bugs/val-labs-trailing-slash.sh \
	tests/bugs/print-crash.sh \
	tests/bugs/keep-all.sh \
	tests/data/datasheet-test.sh \
	tests/libpspp/sparse-xarray-test.sh \
	tests/output/paper-size.sh \
	tests/stats/moments.sh \
	tests/expressions/expressions.sh \
	tests/expressions/epoch.sh \
	tests/expressions/randist.sh \
	tests/expressions/valuelabel.sh \
	tests/expressions/variables.sh \
	tests/expressions/vectors.sh

if GNM_SUPPORT
dist_TESTS += tests/command/get-data-gnm.sh 
endif

if PSQL_SUPPORT
dist_TESTS += tests/command/get-data-psql.sh 
endif

nodist_TESTS = \
	tests/libpspp/abt-test \
	tests/libpspp/bt-test \
	tests/libpspp/heap-test \
	tests/libpspp/hmap-test \
	tests/libpspp/hmapx-test \
	tests/libpspp/ll-test \
	tests/libpspp/llx-test \
	tests/libpspp/range-map-test \
	tests/libpspp/range-set-test \
	tests/libpspp/sparse-array-test \
	tests/libpspp/str-test \
	tests/libpspp/string-map-test \
	tests/libpspp/string-set-test \
	tests/libpspp/stringi-set-test \
	tests/libpspp/tower-test

TESTS = $(dist_TESTS) $(nodist_TESTS)

check_PROGRAMS += \
	$(nodist_TESTS) \
	tests/data/datasheet-test \
	tests/formats/inexactify \
	tests/libpspp/sparse-xarray-test \
	tests/output/render-test

tests_data_datasheet_test_SOURCES = \
	tests/data/datasheet-test.c
tests_data_datasheet_test_LDADD = gl/libgl.la src/libpspp-core.la $(LIBINTL) 
tests_data_datasheet_test_CFLAGS = $(AM_CFLAGS)

tests_libpspp_ll_test_SOURCES = \
	src/libpspp/ll.c \
	src/libpspp/ll.h \
	tests/libpspp/ll-test.c
tests_libpspp_ll_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_ll_test_CFLAGS = $(AM_CFLAGS)

tests_libpspp_llx_test_SOURCES = \
	src/libpspp/ll.c \
	src/libpspp/ll.h \
	src/libpspp/llx.c \
	src/libpspp/llx.h \
	tests/libpspp/llx-test.c
tests_libpspp_llx_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_llx_test_CFLAGS = $(AM_CFLAGS)

tests_libpspp_heap_test_SOURCES = \
	src/libpspp/heap.c \
	src/libpspp/heap.h \
	src/libpspp/pool.c \
	src/libpspp/pool.h \
	tests/libpspp/heap-test.c
tests_libpspp_heap_test_LDADD = gl/libgl.la $(LIBINTL) 
tests_libpspp_heap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_hmap_test_SOURCES = \
	src/libpspp/hmap.c \
	src/libpspp/hmap.h \
	tests/libpspp/hmap-test.c
tests_libpspp_hmap_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_hmap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_hmapx_test_SOURCES = \
	src/libpspp/hmap.c \
	src/libpspp/hmap.h \
	src/libpspp/hmapx.c \
	src/libpspp/hmapx.h \
	tests/libpspp/hmapx-test.c
tests_libpspp_hmapx_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_hmapx_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_abt_test_SOURCES = \
	src/libpspp/abt.c \
	src/libpspp/abt.h \
	tests/libpspp/abt-test.c
tests_libpspp_abt_test_LDADD = gl/libgl.la $(LIBINTL)
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
tests_libpspp_range_map_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_range_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_range_set_test_SOURCES = \
	src/libpspp/bt.c \
	src/libpspp/bt.h \
	src/libpspp/pool.c \
	src/libpspp/pool.h \
	src/libpspp/range-set.c \
	src/libpspp/range-set.h \
	tests/libpspp/range-set-test.c
tests_libpspp_range_set_test_LDADD = gl/libgl.la $(LIBINTL) 
tests_libpspp_range_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_str_test_SOURCES = \
	tests/libpspp/str-test.c
tests_libpspp_str_test_LDADD = src/libpspp/libpspp.la gl/libgl.la $(LIBINTL) 

tests_libpspp_string_map_test_SOURCES = \
	src/libpspp/hash-functions.c \
	src/libpspp/hmap.c \
	src/libpspp/string-map.c \
	src/libpspp/string-set.c \
	tests/libpspp/string-map-test.c
tests_libpspp_string_map_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_string_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_string_set_test_SOURCES = \
	src/libpspp/hash-functions.c \
	src/libpspp/hmap.c \
	src/libpspp/string-set.c \
	tests/libpspp/string-set-test.c
tests_libpspp_string_set_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_string_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_stringi_set_test_SOURCES = \
	src/libpspp/hash-functions.c \
	src/libpspp/hmap.c \
	src/libpspp/pool.c \
	src/libpspp/str.c \
	src/libpspp/stringi-set.c \
	tests/libpspp/stringi-set-test.c
tests_libpspp_stringi_set_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_stringi_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_tower_test_SOURCES = \
	src/libpspp/abt.c \
	src/libpspp/abt.h \
	src/libpspp/pool.c \
	src/libpspp/pool.h \
	src/libpspp/tower.c \
	src/libpspp/tower.h \
	tests/libpspp/tower-test.c
tests_libpspp_tower_test_LDADD = gl/libgl.la $(LIBINTL) 
tests_libpspp_tower_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_sparse_array_test_SOURCES = \
	src/libpspp/sparse-array.c \
	src/libpspp/sparse-array.h \
	src/libpspp/pool.c \
	src/libpspp/pool.h \
	tests/libpspp/sparse-array-test.c
tests_libpspp_sparse_array_test_LDADD = gl/libgl.la $(LIBINTL) 
tests_libpspp_sparse_array_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_sparse_xarray_test_SOURCES = \
	src/libpspp/argv-parser.c \
	src/libpspp/bt.c \
	src/libpspp/deque.c \
	src/libpspp/model-checker.c \
	src/libpspp/range-set.c \
	src/libpspp/sparse-array.c \
	src/libpspp/sparse-xarray.c \
	src/libpspp/str.c \
	src/libpspp/pool.c \
	src/libpspp/tmpfile.c \
	tests/libpspp/sparse-xarray-test.c
tests_libpspp_sparse_xarray_test_LDADD = gl/libgl.la $(LIBINTL) 
tests_libpspp_sparse_xarray_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_formats_inexactify_SOURCES = tests/formats/inexactify.c

noinst_PROGRAMS += tests/dissect-sysfile
tests_dissect_sysfile_SOURCES = \
	src/libpspp/integer-format.c \
	src/libpspp/float-format.c \
	tests/dissect-sysfile.c
tests_dissect_sysfile_LDADD = gl/libgl.la $(LIBINTL) 
tests_dissect_sysfile_CPPFLAGS = $(AM_CPPFLAGS) -DINSTALLDIR=\"$(bindir)\"

check_PROGRAMS += tests/output/render-test
tests_output_render_test_SOURCES = tests/output/render-test.c
tests_output_render_test_LDADD = \
	src/libpspp.la \
	src/libpspp-core.la \
	$(CAIRO_LIBS) \
	$(LIBICONV) \
	$(LIBINTL)

EXTRA_DIST += \
	$(dist_TESTS) \
        tests/Book1.gnm.unzipped \
	tests/weighting.data tests/data-list.data tests/list.data \
	tests/no_case_size.sav \
	tests/coverage.sh tests/test_template \
	tests/v13.sav tests/v14.sav \
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

check-for-export-var-val:
	@if grep -q 'export .*=' $(dist_TESTS) ; then \
	 echo 'One or more tests contain non-portable "export VAR=val" syntax' ; \
	 false ; \
	fi

DIST_HOOKS += check-for-export-var-val

EXTRA_DIST += tests/OChangeLog

# Autotest testsuite

EXTRA_DIST += \
	$(TESTSUITE_AT) \
	$(TESTSUITE) \
	tests/atlocal.in \
	$(srcdir)/package.m4 \
	$(TESTSUITE)
TESTSUITE_AT = \
	tests/testsuite.at \
	tests/language/stats/autorecode.at \
	tests/language/stats/crosstabs.at \
	tests/language/stats/descriptives.at \
	tests/language/stats/frequencies.at \
	tests/language/xforms/compute.at \
	tests/language/xforms/recode.at \
	tests/output/render.at
TESTSUITE = $(srcdir)/tests/testsuite
DISTCLEANFILES += tests/atconfig tests/atlocal $(TESTSUITE)

CHECK_LOCAL += tests_check
tests_check: tests/atconfig tests/atlocal $(TESTSUITE)
	$(SHELL) '$(TESTSUITE)' -C tests AUTOTEST_PATH=tests/output:src/ui/terminal $(TESTSUITEFLAGS)

CLEAN_LOCAL += tests_clean
tests_clean:
	test ! -f '$(TESTSUITE)' || $(SHELL) '$(TESTSUITE)' -C tests --clean

AUTOM4TE = $(SHELL) $(srcdir)/missing --run autom4te
AUTOTEST = $(AUTOM4TE) --language=autotest
$(TESTSUITE): package.m4 $(TESTSUITE_AT)
	$(AUTOTEST) -I '$(srcdir)' -o $@.tmp $@.at
	mv $@.tmp $@

# The `:;' works around a Bash 3.2 bug when the output is not writeable.
$(srcdir)/package.m4: $(top_srcdir)/configure.ac
	:;{ \
	  echo '# Signature of the current package.' && \
	  echo 'm4_define([AT_PACKAGE_NAME],      [$(PACKAGE_NAME)])' && \
	  echo 'm4_define([AT_PACKAGE_TARNAME],   [$(PACKAGE_TARNAME)])' && \
	  echo 'm4_define([AT_PACKAGE_VERSION],   [$(PACKAGE_VERSION)])' && \
	  echo 'm4_define([AT_PACKAGE_STRING],    [$(PACKAGE_STRING)])' && \
	  echo 'm4_define([AT_PACKAGE_BUGREPORT], [$(PACKAGE_BUGREPORT)])' && \
	  echo 'm4_define([AT_PACKAGE_URL],       [$(PACKAGE_URL)])'; \
	} >'$(srcdir)/package.m4'
