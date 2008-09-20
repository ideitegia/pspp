## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/ui/terminal/automake.mk
if WITHGUI
include $(top_srcdir)/src/ui/gui/automake.mk
endif


noinst_LTLIBRARIES += src/ui/libuicommon.la

src_ui_libuicommon_la_SOURCES = \
	src/ui/debugger.c \
	src/ui/debugger.h \
	src/ui/syntax-gen.c \
	src/ui/syntax-gen.h

EXTRA_DIST += src/ui/OChangeLog
