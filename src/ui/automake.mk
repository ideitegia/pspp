## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/ui/terminal/automake.mk
if HAVE_GUI
include $(top_srcdir)/src/ui/gui/automake.mk
endif


noinst_LTLIBRARIES += src/ui/libuicommon.la

src_ui_libuicommon_la_SOURCES = \
	src/ui/command-line.c src/ui/command-line.h \
	src/ui/debugger.c src/ui/debugger.h \
	src/ui/source-init-opts.c src/ui/source-init-opts.h \
	src/ui/syntax-gen.c src/ui/syntax-gen.h

EXTRA_DIST += src/ui/OChangeLog
