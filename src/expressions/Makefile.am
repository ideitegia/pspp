## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/Make.build

noinst_LIBRARIES = libexpressions.a

libexpressions_a_SOURCES = evaluate.c helpers.c helpers.h optimize.c	\
parse.c private.h public.h

BUILT_SOURCES = evaluate.h evaluate.inc operations.h optimize.inc parse.inc

PERL = @PERL@

evaluate.inc: evaluate.inc.pl generate.pl operations.def 
	$(PERL) evaluate.inc.pl -o evaluate.inc

evaluate.h: evaluate.h.pl generate.pl operations.def
	$(PERL) evaluate.h.pl -o evaluate.h

operations.h: operations.h.pl generate.pl operations.def
	$(PERL) operations.h.pl -o operations.h

optimize.inc: optimize.inc.pl generate.pl operations.def
	$(PERL) optimize.inc.pl -o optimize.inc

parse.inc: parse.inc.pl generate.pl operations.def
	$(PERL) parse.inc.pl -o parse.inc
