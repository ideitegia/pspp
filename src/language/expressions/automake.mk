## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/language/expressions/libexpressions.a

$(top_builddir)/src/language/expressions/src_language_expressions_libexpressions_a-evaluate.o: \
	$(top_builddir)/src/language/expressions/evaluate.h \
	$(top_builddir)/src/language/expressions/operations.h \
	$(top_builddir)/src/language/expressions/evaluate.inc

$(top_builddir)/src/language/expressions/src_language_expressions_libexpressions_a-optimize.o: \
	$(top_builddir)/src/language/expressions/optimize.inc

$(top_builddir)/src/language/expressions/src_language_expressions_libexpressions_a-parse.o: \
	$(top_builddir)/src/language/expressions/parse.inc


CLEANFILES += $(expressions_built_sources)

src_language_expressions_libexpressions_a_SOURCES = \
	src/language/expressions/evaluate.c \
	src/language/expressions/helpers.c \
	src/language/expressions/helpers.h \
	src/language/expressions/optimize.c \
	src/language/expressions/parse.c \
	src/language/expressions/private.h \
	src/language/expressions/public.h \
	src/language/expressions/evaluate.inc.pl \
	src/language/expressions/generate.pl \
	src/language/expressions/operations.def \
	src/language/expressions/evaluate.h.pl \
	src/language/expressions/operations.h.pl \
	src/language/expressions/optimize.inc.pl \
	src/language/expressions/parse.inc.pl

expressions_built_sources= \
	src/language/expressions/evaluate.h \
	src/language/expressions/evaluate.inc \
	src/language/expressions/operations.h \
	src/language/expressions/optimize.inc \
	src/language/expressions/parse.inc

src_language_expressions_libexpressions_a_CPPFLAGS = $(AM_CPPFLAGS) \
	-I $(top_builddir)/src/language/expressions \
	-I $(top_srcdir)/src/language/expressions

nodist_src_language_expressions_libexpressions_a_SOURCES = $(expressions_built_sources)


PERL = @PERL@

helpers = $(top_srcdir)/src/language/expressions/generate.pl \
	$(top_srcdir)/src/language/expressions/operations.def

%: %.pl $(helpers)
	@mkdir -p `dirname $@`
	$(PERL) -I $(top_srcdir)/src/language/expressions $< -o $@ -i $(top_srcdir)/src/language/expressions/operations.def

