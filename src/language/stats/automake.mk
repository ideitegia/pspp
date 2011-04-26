## Process this file with automake to produce Makefile.in  -*- makefile -*-

AM_CPPFLAGS += -I$(top_srcdir)/src/language/stats

src_language_stats_built_sources = \
	src/language/stats/crosstabs.c \
	src/language/stats/examine.c \
	src/language/stats/frequencies.c \
	src/language/stats/rank.c \
	src/language/stats/regression.c \
	src/language/stats/t-test.c

language_stats_sources = \
	src/language/stats/aggregate.c \
	src/language/stats/aggregate.h \
	src/language/stats/autorecode.c \
	src/language/stats/binomial.c \
	src/language/stats/binomial.h \
	src/language/stats/chisquare.c  \
	src/language/stats/chisquare.h \
	src/language/stats/cochran.c \
	src/language/stats/cochran.h \
	src/language/stats/correlations.c \
	src/language/stats/descriptives.c \
	src/language/stats/factor.c \
	src/language/stats/flip.c \
	src/language/stats/freq.c \
	src/language/stats/freq.h \
	src/language/stats/friedman.c \
	src/language/stats/friedman.h \
	src/language/stats/kruskal-wallis.c \
	src/language/stats/kruskal-wallis.h \
	src/language/stats/mann-whitney.c \
	src/language/stats/mann-whitney.h \
	src/language/stats/npar.c  \
	src/language/stats/npar.h \
	src/language/stats/npar-summary.c \
	src/language/stats/npar-summary.h \
	src/language/stats/oneway.c \
	src/language/stats/reliability.c \
	src/language/stats/roc.c \
	src/language/stats/roc.h \
	src/language/stats/runs.h \
	src/language/stats/runs.c \
	src/language/stats/sign.c \
	src/language/stats/sign.h \
	src/language/stats/sort-cases.c \
	src/language/stats/sort-criteria.c \
	src/language/stats/sort-criteria.h \
	src/language/stats/wilcoxon.c \
	src/language/stats/wilcoxon.h

EXTRA_DIST += src/language/stats/glm.c

all_q_sources += $(src_language_stats_built_sources:.c=.q)
EXTRA_DIST += $(src_language_stats_built_sources:.c=.q)
CLEANFILES += $(src_language_stats_built_sources)

EXTRA_DIST += src/language/stats/OChangeLog
