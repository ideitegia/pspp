## Process this file with automake to produce Makefile.in  -*- makefile -*-


include $(top_srcdir)/src/output/charts/automake.mk

noinst_LTLIBRARIES += src/output/liboutput.la 

src_output_liboutput_la_SOURCES = \
	src/output/afm.c \
	src/output/afm.h \
	src/output/ascii.c \
	src/output/html.c \
	src/output/htmlP.h \
	src/output/journal.c \
	src/output/journal.h \
	src/output/output.c \
	src/output/output.h \
	src/output/postscript.c \
	src/output/manager.c \
	src/output/manager.h \
	src/output/chart.h \
	src/output/table.c \
	src/output/table.h
if HAVE_CAIRO
src_output_liboutput_la_SOURCES += src/output/cairo.c
endif
if WITHCHARTS
src_output_liboutput_la_SOURCES += src/output/chart.c
else
src_output_liboutput_la_SOURCES += src/output/dummy-chart.c
endif

EXTRA_DIST += src/output/OChangeLog
