## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/language/lexer/automake.mk
include $(top_srcdir)/src/language/xforms/automake.mk
include $(top_srcdir)/src/language/control/automake.mk
include $(top_srcdir)/src/language/dictionary/automake.mk
include $(top_srcdir)/src/language/tests/automake.mk
include $(top_srcdir)/src/language/utilities/automake.mk
include $(top_srcdir)/src/language/stats/automake.mk
include $(top_srcdir)/src/language/data-io/automake.mk
include $(top_srcdir)/src/language/expressions/automake.mk

noinst_LTLIBRARIES +=  src/language/liblanguage.la


src_language_liblanguage_la_SOURCES = \
	src/language/command.c \
	src/language/command.h \
	src/language/command.def \
	$(language_lexer_sources) \
	$(language_xforms_sources) \
	$(language_control_sources) \
	$(language_dictionary_sources) \
	$(language_tests_sources) \
	$(language_utilities_sources) \
	$(language_stats_sources) \
	$(language_data_io_sources) \
	$(language_expressions_sources)


nodist_src_language_liblanguage_la_SOURCES = \
	$(src_language_data_io_built_sources) \
	$(src_language_utilities_built_sources) \
	$(src_language_stats_built_sources)  \
	$(language_tests_built_sources) \
	$(expressions_built_sources)
