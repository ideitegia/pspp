## Process this file with automake to produce Makefile.in  -*- makefile -*-


noinst_LIBRARIES += src/language/stats/libstats.a

src_language_stats_q_sources_q =  \
	src/language/stats/correlations.q \
	src/language/stats/crosstabs.q \
	src/language/stats/examine.q \
	src/language/stats/frequencies.q \
	src/language/stats/means.q \
	src/language/stats/oneway.q \
	src/language/stats/rank.q \
	src/language/stats/regression.q \
	src/language/stats/t-test.q

src_language_stats_q_sources_c = \
	src/language/stats/correlations.c \
	src/language/stats/crosstabs.c \
	src/language/stats/examine.c \
	src/language/stats/frequencies.c \
	src/language/stats/means.c \
	src/language/stats/oneway.c \
	src/language/stats/rank.c \
	src/language/stats/regression.c \
	src/language/stats/t-test.c


EXTRA_DIST += $(src_language_stats_q_sources_q)
nodist_src_language_stats_libstats_a_SOURCES = $(src_language_stats_q_sources_c)
CLEANFILES += $(src_language_stats_q_sources_c)

src_language_stats_libstats_a_SOURCES = \
	src/language/stats/aggregate.c \
	src/language/stats/autorecode.c \
	src/language/stats/descriptives.c \
	src/language/stats/sort-cases.c \
	src/language/stats/sort-criteria.c \
	src/language/stats/sort-criteria.h \
	src/language/stats/flip.c \
	src/language/stats/regression-export.h 
