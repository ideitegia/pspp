## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/output/charts/libcharts.a

src/output/charts/%: AM_CPPFLAGS +=  \
	-I$(top_srcdir)/src/libpspp \
	-I$(top_srcdir)/src/output \
	-I$(top_srcdir)/src/data \
	-I$(top_srcdir)/src/math

chart_sources = \
	src/output/charts/barchart.c \
	src/output/charts/barchart.h \
	src/output/charts/box-whisker.c \
	src/output/charts/box-whisker.h \
	src/output/charts/cartesian.c \
	src/output/charts/cartesian.h \
	src/output/charts/piechart.c \
	src/output/charts/piechart.h \
	src/output/charts/plot-chart.h \
	src/output/charts/plot-chart.c \
	src/output/charts/plot-hist.c \
	src/output/charts/plot-hist.h

if WITHCHARTS
src_output_charts_libcharts_a_SOURCES = \
	$(chart_sources)

EXTRA_DIST += src/output/charts/dummy-chart.c
else
src_output_charts_libcharts_a_SOURCES =  \
	src/output/charts/dummy-chart.c

EXTRA_DIST += $(chart_sources)

AM_CPPFLAGS += -DNO_CHARTS

endif
