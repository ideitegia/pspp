## Process this file with automake to produce Makefile.in  -*- makefile -*-


include $(top_srcdir)/src/output/charts/automake.mk


src/output/%: AM_CPPFLAGS += \
	-I$(top_srcdir)/src/math \
	-I$(top_srcdir)/src/libpspp \
	-I$(top_srcdir)/src/data 

noinst_LIBRARIES += src/output/liboutput.a 




output_sources = \
	src/output/ascii.c \
	src/output/font.h \
	src/output/groff-font.c \
	src/output/html.c \
	src/output/htmlP.h \
	src/output/output.c \
	src/output/output.h \
	src/output/postscript.c \
	src/output/manager.c \
	src/output/manager.h \
	src/output/chart.h \
	src/output/table.c src/output/table.h


if WITHCHARTS
src_output_liboutput_a_SOURCES = $(output_sources) src/output/chart.c
	EXTRA_DIST += src/output/dummy-chart.c
else
src_output_liboutput_a_SOURCES = $(output_sources) src/output/dummy-chart.c
	EXTRA_DIST += src/output/chart.c
endif


