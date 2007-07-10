## Process this file with automake to produce Makefile.in  -*- makefile -*-

AM_CPPFLAGS += -I$(top_srcdir)/src/language/stats

src_language_stats_built_sources = \
	src/language/stats/correlations.c \
	src/language/stats/crosstabs.c \
	src/language/stats/examine.c \
	src/language/stats/frequencies.c \
	src/language/stats/glm.c \
	src/language/stats/means.c \
	src/language/stats/npar.c \
	src/language/stats/oneway.c \
	src/language/stats/rank.c \
	src/language/stats/regression.c \
	src/language/stats/t-test.c

language_stats_sources = \
	src/language/stats/aggregate.c \
	src/language/stats/autorecode.c \
	src/language/stats/binomial.c \
	src/language/stats/binomial.h \
	src/language/stats/chisquare.c \
	src/language/stats/chisquare.h \
	src/language/stats/descriptives.c \
	src/language/stats/npar.h \
	src/language/stats/sort-cases.c \
	src/language/stats/sort-criteria.c \
	src/language/stats/sort-criteria.h \
	src/language/stats/flip.c \
	src/language/stats/freq.c \
	src/language/stats/freq.h \
	src/language/stats/npar-summary.c \
	src/language/stats/npar-summary.h \
	src/language/stats/regression-export.h \
	$(src_language_stats_built_sources)

all_q_sources += $(src_language_stats_built_sources:.c=.q)
EXTRA_DIST += $(src_language_stats_built_sources:.c=.q)
CLEANFILES += $(src_language_stats_built_sources)

