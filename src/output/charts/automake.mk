## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LTLIBRARIES += src/output/charts/libcharts.la

chart_sources = \
	src/output/charts/cartesian.c \
	src/output/charts/cartesian.h \
	src/output/charts/piechart.c \
	src/output/charts/piechart.h \
	src/output/charts/plot-chart.h \
	src/output/charts/plot-chart.c \
	src/output/charts/plot-hist.c \
	src/output/charts/plot-hist.h

if WITHCHARTS
src_output_charts_libcharts_la_SOURCES = \
	$(chart_sources)

EXTRA_DIST += src/output/charts/dummy-chart.c
else
src_output_charts_libcharts_la_SOURCES =  \
	src/output/charts/dummy-chart.c

EXTRA_DIST += $(chart_sources)

AM_CPPFLAGS += -DNO_CHARTS

endif

EXTRA_DIST += src/output/charts/OChangeLog
