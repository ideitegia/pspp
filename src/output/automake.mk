## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LTLIBRARIES += src/output/liboutput.la 

src_output_liboutput_la_CPPFLAGS = $(LIBXML2_CFLAGS) $(AM_CPPFLAGS) 

src_output_liboutput_la_SOURCES = \
	src/output/ascii.c \
	src/output/chart.c \
	src/output/chart.h \
	src/output/charts/box-whisker.c \
	src/output/charts/box-whisker.h \
	src/output/charts/cartesian.c \
	src/output/charts/cartesian.h \
	src/output/charts/np-plot.c \
	src/output/charts/np-plot.h \
	src/output/charts/piechart.c \
	src/output/charts/piechart.h \
	src/output/charts/plot-chart.c \
	src/output/charts/plot-chart.h \
	src/output/charts/plot-hist.c \
	src/output/charts/plot-hist.h \
	src/output/charts/roc-chart.c \
	src/output/charts/roc-chart.h \
	src/output/html.c \
	src/output/htmlP.h \
	src/output/journal.c \
	src/output/journal.h \
	src/output/manager.c \
	src/output/manager.h \
	src/output/odt.c \
	src/output/output.c \
	src/output/output.h \
	src/output/table.c \
	src/output/table.h
if HAVE_CAIRO
src_output_liboutput_la_SOURCES += src/output/cairo.c
endif

EXTRA_DIST += src/output/OChangeLog
