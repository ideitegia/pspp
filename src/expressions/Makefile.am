## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/Make.build

noinst_LIBRARIES = libexpressions.a

libexpressions_a_SOURCES = evaluate.c helpers.c helpers.h optimize.c	\
	parse.c private.h public.h \
	evaluate.inc.pl generate.pl operations.def \
	evaluate.h.pl operations.h.pl optimize.inc.pl parse.inc.pl

BUILT_SOURCES = evaluate.h evaluate.inc operations.h optimize.inc parse.inc
CLEANFILES = evaluate.h evaluate.inc operations.h optimize.inc parse.inc

PERL = @PERL@

helpers = $(srcdir)/generate.pl $(srcdir)/operations.def

evaluate.inc: $(srcdir)/evaluate.inc.pl $(helpers)
	$(PERL) -I $(srcdir) $< -o $@ -i $(srcdir)/operations.def

evaluate.h: $(srcdir)/evaluate.h.pl $(helpers)
	$(PERL) -I $(srcdir) $< -o $@ -i $(srcdir)/operations.def

operations.h: $(srcdir)/operations.h.pl $(helpers)
	$(PERL) -I $(srcdir) $< -o $@ -i $(srcdir)/operations.def

optimize.inc: $(srcdir)/optimize.inc.pl $(helpers)
	$(PERL) -I $(srcdir) $< -o $@ -i $(srcdir)/operations.def

parse.inc: $(srcdir)/parse.inc.pl $(helpers)
	$(PERL) -I $(srcdir) $< -o $@ -i $(srcdir)/operations.def
