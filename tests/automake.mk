## Process this file with automake to produce Makefile.in  -*- makefile -*-

TESTS_ENVIRONMENT = top_srcdir='$(top_srcdir)' top_builddir='$(top_builddir)'
TESTS_ENVIRONMENT += PERL='$(PERL)'

# Allow locale_charset to find charset.alias before running "make install".
TESTS_ENVIRONMENT += CHARSETALIASDIR='$(abs_top_builddir)/gl'

TESTS_ENVIRONMENT += LC_ALL=C
TESTS_ENVIRONMENT += EXEEXT=$(EXEEXT)

dist_TESTS = \
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
	tests/bugs/big-input.sh \
	tests/bugs/big-input-2.sh \
	tests/bugs/case-map.sh \
	tests/bugs/comment-at-eof.sh \
	tests/bugs/compression.sh \
	tests/bugs/curtailed.sh \
	tests/bugs/data-crash.sh \
	tests/bugs/get.sh \
	tests/bugs/get-no-file.sh \
	tests/bugs/if_crash.sh \
	tests/bugs/input-crash.sh \
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
	tests/bugs/keep-all.sh \
	tests/data/datasheet-test.sh \
	tests/libpspp/sparse-xarray-test.sh \
	tests/output/paper-size.sh \
	tests/expressions/randist.sh \
	tests/expressions/valuelabel.sh \
	tests/expressions/variables.sh \
	tests/expressions/vectors.sh

TESTS = $(dist_TESTS) $(nodist_TESTS)

check_PROGRAMS += \
	$(nodist_TESTS) \
	tests/data/datasheet-test \
	tests/formats/inexactify \
	tests/libpspp/abt-test \
	tests/libpspp/bt-test \
	tests/libpspp/heap-test \
	tests/libpspp/hmap-test \
	tests/libpspp/hmapx-test \
	tests/libpspp/i18n-test \
	tests/libpspp/ll-test \
	tests/libpspp/llx-test \
	tests/libpspp/range-map-test \
	tests/libpspp/range-set-test \
	tests/libpspp/sparse-array-test \
	tests/libpspp/sparse-xarray-test \
	tests/libpspp/str-test \
	tests/libpspp/string-map-test \
	tests/libpspp/stringi-map-test \
	tests/libpspp/string-set-test \
	tests/libpspp/stringi-set-test \
	tests/libpspp/tower-test \
	tests/output/render-test

tests_data_datasheet_test_SOURCES = \
	tests/data/datasheet-test.c
tests_data_datasheet_test_LDADD = src/libpspp-core.la $(LIBINTL) 
tests_data_datasheet_test_CFLAGS = $(AM_CFLAGS)

tests_libpspp_ll_test_SOURCES = \
	src/libpspp/ll.c \
	tests/libpspp/ll-test.c
tests_libpspp_ll_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_ll_test_CFLAGS = $(AM_CFLAGS)

tests_libpspp_llx_test_SOURCES = \
	src/libpspp/ll.c \
	src/libpspp/llx.c \
	tests/libpspp/llx-test.c
tests_libpspp_llx_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_llx_test_CFLAGS = $(AM_CFLAGS)

tests_libpspp_heap_test_SOURCES = \
	src/libpspp/heap.c \
	src/libpspp/pool.c \
	src/libpspp/temp-file.c \
	tests/libpspp/heap-test.c
tests_libpspp_heap_test_LDADD = gl/libgl.la $(LIBINTL) 
tests_libpspp_heap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_hmap_test_SOURCES = \
	src/libpspp/hmap.c \
	tests/libpspp/hmap-test.c
tests_libpspp_hmap_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_hmap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_hmapx_test_SOURCES = \
	src/libpspp/hmap.c \
	src/libpspp/hmapx.c \
	tests/libpspp/hmapx-test.c
tests_libpspp_hmapx_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_hmapx_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_i18n_test_SOURCES = tests/libpspp/i18n-test.c
tests_libpspp_i18n_test_LDADD = src/libpspp/libpspp.la gl/libgl.la $(LIBINTL) 

tests_libpspp_abt_test_SOURCES = \
	src/libpspp/abt.c \
	tests/libpspp/abt-test.c
tests_libpspp_abt_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_abt_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_bt_test_SOURCES = \
	src/libpspp/bt.c \
	tests/libpspp/bt-test.c
tests_libpspp_bt_test_LDADD = gl/libgl.la
tests_libpspp_bt_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_range_map_test_SOURCES = \
	src/libpspp/bt.c \
	src/libpspp/range-map.c \
	tests/libpspp/range-map-test.c
tests_libpspp_range_map_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_range_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_range_set_test_SOURCES = \
	src/libpspp/bt.c \
	src/libpspp/pool.c \
	src/libpspp/range-set.c \
	src/libpspp/temp-file.c \
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

tests_libpspp_stringi_map_test_SOURCES = \
	src/libpspp/hash-functions.c \
	src/libpspp/hmap.c \
	src/libpspp/pool.c \
	src/libpspp/str.c \
	src/libpspp/stringi-map.c \
	src/libpspp/string-set.c \
	src/libpspp/stringi-set.c \
	src/libpspp/temp-file.c \
	tests/libpspp/stringi-map-test.c
tests_libpspp_stringi_map_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_stringi_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

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
	src/libpspp/temp-file.c \
	tests/libpspp/stringi-set-test.c
tests_libpspp_stringi_set_test_LDADD = gl/libgl.la $(LIBINTL)
tests_libpspp_stringi_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_tower_test_SOURCES = \
	src/libpspp/abt.c \
	src/libpspp/pool.c \
	src/libpspp/temp-file.c \
	src/libpspp/tower.c \
	tests/libpspp/tower-test.c
tests_libpspp_tower_test_LDADD = gl/libgl.la $(LIBINTL) 
tests_libpspp_tower_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_sparse_array_test_SOURCES = \
	src/libpspp/sparse-array.c \
	src/libpspp/pool.c \
	tests/libpspp/sparse-array-test.c \
	src/libpspp/temp-file.c
tests_libpspp_sparse_array_test_LDADD = gl/libgl.la $(LIBINTL) 
tests_libpspp_sparse_array_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_sparse_xarray_test_SOURCES = \
	src/libpspp/argv-parser.c \
	src/libpspp/bt.c \
	src/libpspp/deque.c \
	src/libpspp/ext-array.c \
	src/libpspp/model-checker.c \
	src/libpspp/range-set.c \
	src/libpspp/sparse-array.c \
	src/libpspp/sparse-xarray.c \
	src/libpspp/str.c \
	src/libpspp/pool.c \
	src/libpspp/temp-file.c \
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
	tests/weighting.data \
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
	tests/formats/num-out.pl \
        tests/language/data-io/Book1.gnm.unzipped

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
	tests/testsuite.in \
	$(TESTSUITE_AT) \
	$(TESTSUITE) \
	tests/atlocal.in \
	$(srcdir)/package.m4 \
	$(TESTSUITE)

TESTSUITE_AT = \
	tests/data/calendar.at \
	tests/data/data-in.at \
	tests/data/sys-file.at \
	tests/language/command.at \
	tests/language/control/do-if.at \
	tests/language/control/do-repeat.at \
	tests/language/control/loop.at \
	tests/language/data-io/add-files.at \
	tests/language/data-io/data-list.at \
	tests/language/data-io/data-reader.at \
	tests/language/data-io/file-handle.at \
	tests/language/data-io/get-data-gnm.at \
	tests/language/data-io/get-data-psql.at \
	tests/language/data-io/get-data-txt.at \
	tests/language/data-io/list.at \
	tests/language/data-io/match-files.at \
	tests/language/data-io/print.at \
	tests/language/data-io/save.at \
	tests/language/data-io/save-translate.at \
	tests/language/dictionary/attributes.at \
	tests/language/dictionary/missing-values.at \
	tests/language/dictionary/mrsets.at \
	tests/language/dictionary/rename-variables.at \
	tests/language/expressions/evaluate.at \
	tests/language/lexer/variable-parser.at \
	tests/language/stats/aggregate.at \
	tests/language/stats/autorecode.at \
	tests/language/stats/correlations.at \
	tests/language/stats/crosstabs.at \
	tests/language/stats/descriptives.at \
	tests/language/stats/examine.at \
	tests/language/stats/factor.at \
	tests/language/stats/flip.at \
	tests/language/stats/frequencies.at \
	tests/language/stats/npar.at \
	tests/language/stats/oneway.at \
	tests/language/stats/rank.at \
	tests/language/stats/regression.at \
	tests/language/stats/reliability.at \
	tests/language/stats/roc.at \
	tests/language/utilities/insert.at \
	tests/language/utilities/permissions.at \
	tests/language/utilities/set.at \
	tests/language/utilities/title.at \
	tests/language/xforms/compute.at \
	tests/language/xforms/count.at \
	tests/language/xforms/recode.at \
	tests/language/xforms/select-if.at \
	tests/libpspp/abt.at \
	tests/libpspp/bt.at \
	tests/libpspp/heap.at \
	tests/libpspp/hmap.at \
	tests/libpspp/hmapx.at \
	tests/libpspp/i18n.at \
	tests/libpspp/ll.at \
	tests/libpspp/llx.at \
	tests/libpspp/range-map.at \
	tests/libpspp/range-set.at \
	tests/libpspp/sparse-array.at \
	tests/libpspp/str.at \
	tests/libpspp/string-map.at \
	tests/libpspp/stringi-map.at \
	tests/libpspp/string-set.at \
	tests/libpspp/stringi-set.at \
	tests/libpspp/tower.at \
	tests/math/moments.at \
	tests/output/render.at \
	tests/output/charts.at \
	tests/ui/terminal/main.at \
	tests/perl-module.at

TESTSUITE = $(srcdir)/tests/testsuite
DISTCLEANFILES += tests/atconfig tests/atlocal $(TESTSUITE)

$(srcdir)/tests/testsuite.at: tests/testsuite.in Makefile
	cp $< $@
	for t in $(TESTSUITE_AT); do \
	  echo "m4_include([$$t])" >> $@ ;\
	done


CHECK_LOCAL += tests_check
tests_check: tests/atconfig tests/atlocal $(TESTSUITE) $(check_PROGRAMS)
	$(SHELL) '$(TESTSUITE)' -C tests AUTOTEST_PATH=tests/libpspp:tests/output:src/ui/terminal $(TESTSUITEFLAGS)

CLEAN_LOCAL += tests_clean
tests_clean:
	test ! -f '$(TESTSUITE)' || $(SHELL) '$(TESTSUITE)' -C tests --clean

AUTOM4TE = $(SHELL) $(srcdir)/build-aux/missing --run autom4te
AUTOTEST = $(AUTOM4TE) --language=autotest
$(TESTSUITE): package.m4 $(srcdir)/tests/testsuite.at $(TESTSUITE_AT) 
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
