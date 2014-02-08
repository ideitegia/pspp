## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LTLIBRARIES += src/output/liboutput.la 

src_output_liboutput_la_CPPFLAGS = $(LIBXML2_CFLAGS) $(AM_CPPFLAGS) 

src_output_liboutput_la_SOURCES = \
	src/output/ascii.c \
	src/output/ascii.h \
	src/output/chart-item-provider.h \
	src/output/chart-item.c \
	src/output/chart-item.h \
	src/output/charts/boxplot.c \
	src/output/charts/boxplot.h \
	src/output/charts/np-plot.c \
	src/output/charts/np-plot.h \
	src/output/charts/piechart.c \
	src/output/charts/piechart.h \
	src/output/charts/plot-hist.c \
	src/output/charts/plot-hist.h \
	src/output/charts/roc-chart.c \
	src/output/charts/roc-chart.h \
	src/output/charts/spreadlevel-plot.c \
	src/output/charts/spreadlevel-plot.h \
	src/output/charts/scree.c \
	src/output/charts/scree.h \
	src/output/csv.c \
	src/output/driver-provider.h \
	src/output/driver.c \
	src/output/driver.h \
	src/output/html.c \
	src/output/journal.c \
	src/output/journal.h \
	src/output/measure.c \
	src/output/measure.h \
	src/output/message-item.c \
	src/output/message-item.h \
	src/output/msglog.c \
	src/output/msglog.h \
	src/output/options.c \
	src/output/options.h \
	src/output/output-item-provider.h \
	src/output/output-item.c \
	src/output/output-item.h \
	src/output/render.c \
	src/output/render.h \
	src/output/tab.c \
	src/output/tab.h \
	src/output/table-casereader.c \
	src/output/table-item.c \
	src/output/table-item.h \
	src/output/table-paste.c \
	src/output/table-provider.h \
	src/output/table-select.c \
	src/output/table-transpose.c \
	src/output/table.c \
	src/output/table.h \
	src/output/text-item.c \
	src/output/text-item.h
if HAVE_CAIRO
src_output_liboutput_la_SOURCES += \
	src/output/cairo-chart.c \
	src/output/cairo-chart.h \
	src/output/cairo.c \
	src/output/cairo.h \
	src/output/charts/boxplot-cairo.c \
	src/output/charts/np-plot-cairo.c \
	src/output/charts/piechart-cairo.c \
	src/output/charts/plot-hist-cairo.c \
	src/output/charts/roc-chart-cairo.c \
	src/output/charts/scree-cairo.c \
	src/output/charts/spreadlevel-cairo.c
endif
if ODF_WRITE_SUPPORT
src_output_liboutput_la_SOURCES += src/output/odt.c
endif

EXTRA_DIST += \
	src/output/README \
	src/output/mk-class-boilerplate
