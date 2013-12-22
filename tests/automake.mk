## Process this file with automake to produce Makefile.in  -*- makefile -*-

check_PROGRAMS += \
	tests/data/datasheet-test \
	tests/data/sack \
	tests/data/inexactify \
	tests/language/lexer/command-name-test \
	tests/language/lexer/scan-test \
	tests/language/lexer/segment-test \
	tests/libpspp/abt-test \
	tests/libpspp/bt-test \
	tests/libpspp/cmac-aes256-test \
	tests/libpspp/encoding-guesser-test \
	tests/libpspp/heap-test \
	tests/libpspp/hmap-test \
	tests/libpspp/hmapx-test \
	tests/libpspp/i18n-test \
	tests/libpspp/line-reader-test \
	tests/libpspp/ll-test \
	tests/libpspp/llx-test \
	tests/libpspp/range-map-test \
	tests/libpspp/range-set-test \
	tests/libpspp/range-tower-test \
	tests/libpspp/sparse-array-test \
	tests/libpspp/sparse-xarray-test \
	tests/libpspp/str-test \
	tests/libpspp/string-map-test \
	tests/libpspp/stringi-map-test \
	tests/libpspp/string-set-test \
	tests/libpspp/stringi-set-test \
	tests/libpspp/tower-test \
	tests/libpspp/u8-istream-test \
	tests/libpspp/zip-test \
	tests/output/render-test

check-programs: $(check_PROGRAMS)

tests_data_datasheet_test_SOURCES = \
	tests/data/datasheet-test.c
tests_data_datasheet_test_LDADD = src/libpspp-core.la
tests_data_datasheet_test_CFLAGS = $(AM_CFLAGS)

tests_data_sack_SOURCES = \
	tests/data/sack.c
tests_data_sack_LDADD = src/libpspp-core.la 
tests_data_sack_CFLAGS = $(AM_CFLAGS)

tests_libpspp_line_reader_test_SOURCES = tests/libpspp/line-reader-test.c
tests_libpspp_line_reader_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_ll_test_SOURCES = \
	src/libpspp/ll.c \
	tests/libpspp/ll-test.c
tests_libpspp_ll_test_CFLAGS = $(AM_CFLAGS)

tests_libpspp_llx_test_SOURCES = \
	src/libpspp/ll.c \
	src/libpspp/llx.c \
	tests/libpspp/llx-test.c
tests_libpspp_llx_test_CFLAGS = $(AM_CFLAGS)

tests_libpspp_encoding_guesser_test_SOURCES = \
	tests/libpspp/encoding-guesser-test.c
tests_libpspp_encoding_guesser_test_LDADD = \
	src/libpspp/liblibpspp.la \
	gl/libgl.la

tests_libpspp_heap_test_SOURCES = \
	tests/libpspp/heap-test.c
tests_libpspp_heap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_heap_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_hmap_test_SOURCES = \
	src/libpspp/hmap.c \
	tests/libpspp/hmap-test.c
tests_libpspp_hmap_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_hmapx_test_SOURCES = \
	src/libpspp/hmap.c \
	src/libpspp/hmapx.c \
	tests/libpspp/hmapx-test.c
tests_libpspp_hmapx_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_i18n_test_SOURCES = tests/libpspp/i18n-test.c
tests_libpspp_i18n_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la 

tests_libpspp_abt_test_SOURCES = \
	src/libpspp/abt.c \
	tests/libpspp/abt-test.c
tests_libpspp_abt_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_bt_test_SOURCES = \
	src/libpspp/bt.c \
	tests/libpspp/bt-test.c
tests_libpspp_bt_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_cmac_aes256_test_SOURCES = \
	src/libpspp/cmac-aes256.c \
	tests/libpspp/cmac-aes256-test.c
tests_libpspp_cmac_aes256_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_range_map_test_SOURCES = \
	src/libpspp/bt.c \
	src/libpspp/range-map.c \
	tests/libpspp/range-map-test.c
tests_libpspp_range_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_range_set_test_SOURCES = \
	tests/libpspp/range-set-test.c
tests_libpspp_range_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_range_set_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_range_tower_test_SOURCES = \
	tests/libpspp/range-tower-test.c
tests_libpspp_range_tower_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_range_tower_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_str_test_SOURCES = \
	tests/libpspp/str-test.c
tests_libpspp_str_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la 

tests_libpspp_string_map_test_SOURCES = \
	tests/libpspp/string-map-test.c
tests_libpspp_string_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_string_map_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_stringi_map_test_SOURCES = \
	tests/libpspp/stringi-map-test.c
tests_libpspp_stringi_map_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_stringi_map_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_string_set_test_SOURCES = \
	src/libpspp/hash-functions.c \
	src/libpspp/hmap.c \
	src/libpspp/string-set.c \
	tests/libpspp/string-set-test.c
tests_libpspp_string_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10

tests_libpspp_stringi_set_test_SOURCES = \
	tests/libpspp/stringi-set-test.c
tests_libpspp_stringi_set_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_stringi_set_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_tower_test_SOURCES = \
	tests/libpspp/tower-test.c
tests_libpspp_tower_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_tower_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_u8_istream_test_SOURCES = tests/libpspp/u8-istream-test.c
tests_libpspp_u8_istream_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_sparse_array_test_SOURCES = \
	tests/libpspp/sparse-array-test.c 
tests_libpspp_sparse_array_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_sparse_array_test_LDADD = src/libpspp/liblibpspp.la gl/libgl.la

tests_libpspp_sparse_xarray_test_SOURCES = \
	tests/libpspp/sparse-xarray-test.c
tests_libpspp_sparse_xarray_test_CPPFLAGS = $(AM_CPPFLAGS) -DASSERT_LEVEL=10
tests_libpspp_sparse_xarray_test_LDADD = src/libpspp/liblibpspp.la \
	src/libpspp-core.la \
	gl/libgl.la 

tests_data_inexactify_SOURCES = tests/data/inexactify.c

check_PROGRAMS += tests/language/lexer/command-name-test
tests_language_lexer_command_name_test_SOURCES = \
	src/data/identifier.c \
	src/language/lexer/command-name.c \
	tests/language/lexer/command-name-test.c
tests_language_lexer_command_name_test_LDADD = \
	src/libpspp/liblibpspp.la \
	gl/libgl.la 
tests_language_lexer_command_name_test_CFLAGS = $(AM_CFLAGS)

check_PROGRAMS += tests/language/lexer/scan-test
tests_language_lexer_scan_test_SOURCES = \
	src/data/identifier.c \
	src/language/lexer/command-name.c \
	src/language/lexer/scan.c \
	src/language/lexer/segment.c \
	src/language/lexer/token.c \
	tests/language/lexer/scan-test.c
tests_language_lexer_scan_test_CFLAGS = $(AM_CFLAGS)
tests_language_lexer_scan_test_LDADD = \
	src/libpspp/liblibpspp.la \
	gl/libgl.la 

check_PROGRAMS += tests/language/lexer/segment-test
tests_language_lexer_segment_test_SOURCES = \
	src/data/identifier.c \
	src/language/lexer/command-name.c \
	src/language/lexer/segment.c \
	tests/language/lexer/segment-test.c
tests_language_lexer_segment_test_CFLAGS = $(AM_CFLAGS)
tests_language_lexer_segment_test_LDADD = \
	src/libpspp/liblibpspp.la \
	gl/libgl.la 

check_PROGRAMS += tests/libpspp/zip-test
tests_libpspp_zip_test_SOURCES = \
	tests/libpspp/zip-test.c

tests_libpspp_zip_test_CFLAGS = $(AM_CFLAGS)
tests_libpspp_zip_test_LDADD = \
	src/libpspp/liblibpspp.la \
	src/libpspp-core.la \
	gl/libgl.la 

check_PROGRAMS += tests/output/render-test
tests_output_render_test_SOURCES = tests/output/render-test.c
tests_output_render_test_LDADD = \
	src/libpspp.la \
	src/libpspp-core.la \
	$(CAIRO_LIBS)

EXTRA_DIST += \
	tests/coverage.sh \
	tests/data/bcd-in.expected.cmp.gz \
	tests/data/binhex-in.expected.cmp.gz \
	tests/data/binhex-out.expected.gz \
	tests/data/legacy-in.expected.cmp.gz \
	tests/data/num-in.expected.gz \
	tests/data/num-out-cmp.pl \
	tests/data/num-out.expected.cmp.gz \
	tests/data/v13.sav \
	tests/data/v14.sav \
        tests/language/data-io/Book1.gnm.unzipped \
        tests/language/data-io/test.ods

CLEANFILES += *.save pspp.* foo*

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
	tests/data/data-out.at \
	tests/data/datasheet-test.at \
	tests/data/dictionary.at \
	tests/data/format-guesser.at \
	tests/data/por-file.at \
	tests/data/sys-file-reader.at \
	tests/data/sys-file.at \
	tests/data/sys-file-encryption.at \
	tests/language/command.at \
	tests/language/control/do-if.at \
	tests/language/control/do-repeat.at \
	tests/language/control/loop.at \
	tests/language/control/temporary.at \
	tests/language/data-io/add-files.at \
	tests/language/data-io/data-list.at \
	tests/language/data-io/data-reader.at \
	tests/language/data-io/dataset.at \
	tests/language/data-io/file-handle.at \
	tests/language/data-io/get-data-spreadsheet.at \
	tests/language/data-io/get-data-psql.at \
	tests/language/data-io/get-data-txt.at \
	tests/language/data-io/get.at \
	tests/language/data-io/inpt-pgm.at \
	tests/language/data-io/list.at \
	tests/language/data-io/match-files.at \
	tests/language/data-io/print-space.at \
	tests/language/data-io/print.at \
	tests/language/data-io/save.at \
	tests/language/data-io/save-translate.at \
	tests/language/data-io/update.at \
	tests/language/dictionary/attributes.at \
	tests/language/dictionary/delete-variables.at \
	tests/language/dictionary/formats.at \
	tests/language/dictionary/missing-values.at \
	tests/language/dictionary/mrsets.at \
	tests/language/dictionary/rename-variables.at \
	tests/language/dictionary/split-file.at \
	tests/language/dictionary/sys-file-info.at \
	tests/language/dictionary/value-labels.at \
	tests/language/dictionary/variable-display.at \
	tests/language/dictionary/vector.at \
	tests/language/dictionary/weight.at \
	tests/language/expressions/evaluate.at \
	tests/language/expressions/parse.at \
	tests/language/lexer/command-name.at \
	tests/language/lexer/lexer.at \
	tests/language/lexer/q2c.at \
	tests/language/lexer/scan.at \
	tests/language/lexer/segment.at \
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
	tests/language/stats/glm.at \
	tests/language/stats/logistic.at \
	tests/language/stats/means.at \
	tests/language/stats/npar.at \
	tests/language/stats/oneway.at \
	tests/language/stats/quick-cluster.at \
	tests/language/stats/rank.at \
	tests/language/stats/regression.at \
	tests/language/stats/reliability.at \
	tests/language/stats/roc.at \
	tests/language/stats/sort-cases.at \
	tests/language/stats/t-test.at \
	tests/language/utilities/cache.at \
	tests/language/utilities/cd.at \
	tests/language/utilities/date.at \
	tests/language/utilities/insert.at \
	tests/language/utilities/permissions.at \
	tests/language/utilities/set.at \
	tests/language/utilities/show.at \
	tests/language/utilities/title.at \
	tests/language/xforms/compute.at \
	tests/language/xforms/count.at \
	tests/language/xforms/recode.at \
	tests/language/xforms/sample.at \
	tests/language/xforms/select-if.at \
	tests/libpspp/abt.at \
	tests/libpspp/bt.at \
	tests/libpspp/encoding-guesser.at \
	tests/libpspp/float-format.at \
	tests/libpspp/heap.at \
	tests/libpspp/hmap.at \
	tests/libpspp/hmapx.at \
	tests/libpspp/i18n.at \
	tests/libpspp/line-reader.at \
	tests/libpspp/ll.at \
	tests/libpspp/llx.at \
	tests/libpspp/range-map.at \
	tests/libpspp/range-set.at \
	tests/libpspp/range-tower.at \
	tests/libpspp/sparse-array.at \
	tests/libpspp/sparse-xarray-test.at \
	tests/libpspp/str.at \
	tests/libpspp/string-map.at \
	tests/libpspp/stringi-map.at \
	tests/libpspp/string-set.at \
	tests/libpspp/stringi-set.at \
	tests/libpspp/tower.at \
	tests/libpspp/u8-istream.at \
	tests/libpspp/zip.at \
	tests/math/moments.at \
	tests/math/randist.at \
	tests/output/ascii.at \
	tests/output/charts.at \
	tests/output/output.at \
	tests/output/paper-size.at \
	tests/output/render.at \
	tests/ui/terminal/main.at \
	tests/perl-module.at

TESTSUITE = $(srcdir)/tests/testsuite
DISTCLEANFILES += tests/atconfig tests/atlocal $(TESTSUITE)
AUTOTEST_PATH = tests/data:tests/language/lexer:tests/libpspp:tests/output:src/ui/terminal:utilities

$(srcdir)/tests/testsuite.at: tests/testsuite.in tests/automake.mk
	cp $< $@
	for t in $(TESTSUITE_AT); do \
	  echo "m4_include([$$t])" >> $@ ;\
	done
EXTRA_DIST += tests/testsuite.at

CHECK_LOCAL += tests_check
tests_check: tests/atconfig tests/atlocal $(TESTSUITE) $(check_PROGRAMS)
	XTERM_LOCALE='' $(SHELL) '$(TESTSUITE)' -C tests AUTOTEST_PATH=$(AUTOTEST_PATH) $(TESTSUITEFLAGS)

CLEAN_LOCAL += tests_clean
tests_clean:
	test ! -f '$(TESTSUITE)' || $(SHELL) '$(TESTSUITE)' -C tests --clean

AUTOM4TE = $(SHELL) $(srcdir)/build-aux/missing --run autom4te
AUTOTEST = $(AUTOM4TE) --language=autotest
$(TESTSUITE): package.m4 $(srcdir)/tests/testsuite.at $(TESTSUITE_AT) 
	$(AUTOTEST) -I '$(srcdir)' $@.at | sed 's/@<00A0>@/ /g' > $@.tmp
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

# valgrind support for Autotest testsuite

valgrind_wrappers = \
	tests/valgrind/datasheet-test \
	tests/valgrind/command-name-test \
	tests/valgrind/scan-test \
	tests/valgrind/segment-test \
	tests/valgrind/abt-test \
	tests/valgrind/bt-test \
	tests/valgrind/encoding-guesser-test \
	tests/valgrind/heap-test \
	tests/valgrind/hmap-test \
	tests/valgrind/hmapx-test \
	tests/valgrind/i18n-test \
	tests/valgrind/ll-test \
	tests/valgrind/llx-test \
	tests/valgrind/range-map-test \
	tests/valgrind/range-set-test \
	tests/valgrind/range-tower-test \
	tests/valgrind/sparse-array-test \
	tests/valgrind/sparse-xarray-test \
	tests/valgrind/str-test \
	tests/valgrind/string-map-test \
	tests/valgrind/stringi-map-test \
	tests/valgrind/string-set-test \
	tests/valgrind/stringi-set-test \
	tests/valgrind/tower-test \
	tests/valgrind/u8-istream-test \
	tests/valgrind/render-test \
	tests/valgrind/pspp

$(valgrind_wrappers): tests/valgrind-wrapper.in
	@$(MKDIR_P) tests/valgrind
	sed -e 's,[@]wrap_program[@],$@,' \
		$(top_srcdir)/tests/valgrind-wrapper.in > $@.tmp
	chmod +x $@.tmp
	mv $@.tmp $@
CLEANFILES += $(valgrind_wrappers)
EXTRA_DIST += tests/valgrind-wrapper.in

VALGRIND = $(SHELL) $(abs_top_builddir)/libtool --mode=execute valgrind --log-file=valgrind.%p --leak-check=full --num-callers=20
check-valgrind: all tests/atconfig tests/atlocal $(TESTSUITE) $(valgrind_wrappers)
	XTERM_LOCALE='' $(SHELL) '$(TESTSUITE)' -C tests VALGRIND='$(VALGRIND)' AUTOTEST_PATH='tests/valgrind:$(AUTOTEST_PATH)' -d $(TESTSUITEFLAGS)
	@echo
	@echo '--------------------------------'
	@echo 'Valgrind output is in:'
	@echo 'tests/testsuite.dir/*/valgrind.*'
	@echo '--------------------------------'
