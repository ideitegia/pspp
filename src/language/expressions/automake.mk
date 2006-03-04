## Process this file with automake to produce Makefile.in  -*- makefile -*-

src/language/expressions/%: AM_CPPFLAGS += -I$(top_srcdir)/src/libpspp \
  -I$(top_srcdir)/src/data \
  -I$(top_srcdir)/src/math \
  -I$(top_srcdir)/src/language/lexer \
  -I$(top_srcdir)/src/language/expressions \
  -I$(top_builddir)/src/language/expressions \
  -I$(top_srcdir)/src/language

noinst_LIBRARIES += src/language/expressions/libexpressions.a

src/language/expressions/evaluate.o: src/language/expressions/evaluate.h src/language/expressions/operations.h src/language/expressions/evaluate.inc

src/language/expressions/optimize.o: src/language/expressions/optimize.inc

src/language/expressions/parse.o: src/language/expressions/parse.inc

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


nodist_src_language_expressions_libexpressions_a_SOURCES = $(expressions_built_sources)


PERL = @PERL@

helpers = $(top_srcdir)/src/language/expressions/generate.pl \
	$(top_srcdir)/src/language/expressions/operations.def

%: %.pl $(helpers)
	@mkdir -p `dirname $@`
	$(PERL) -I $(top_srcdir)/src/language/expressions $< -o $@ -i $(top_srcdir)/src/language/expressions/operations.def

