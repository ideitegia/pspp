## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/language/stats/libstats.a

src_language_stats_built_sources = \
	src/language/stats/correlations.c \
	src/language/stats/crosstabs.c \
	src/language/stats/examine.c \
	src/language/stats/frequencies.c \
	src/language/stats/means.c \
	src/language/stats/oneway.c \
	src/language/stats/rank.c \
	src/language/stats/regression.c \
	src/language/stats/t-test.c

all_q_sources += $(src_language_stats_built_sources:.c=.q)
EXTRA_DIST += $(src_language_stats_built_sources:.c=.q)
nodist_src_language_stats_libstats_a_SOURCES = $(src_language_stats_built_sources)
CLEANFILES += $(src_language_stats_built_sources)

src_language_stats_libstats_a_CPPFLAGS = $(AM_CPPFLAGS) \
	-I$(top_srcdir)/src/language/stats

src_language_stats_libstats_a_SOURCES = \
	src/language/stats/aggregate.c \
	src/language/stats/autorecode.c \
	src/language/stats/descriptives.c \
	src/language/stats/sort-cases.c \
	src/language/stats/sort-criteria.c \
	src/language/stats/sort-criteria.h \
	src/language/stats/flip.c \
	src/language/stats/regression-export.h 
