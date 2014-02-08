## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/ui/terminal/automake.mk
include $(top_srcdir)/src/ui/gui/automake.mk


noinst_LTLIBRARIES += src/ui/libuicommon.la

src_ui_libuicommon_la_SOURCES = \
	src/ui/source-init-opts.c src/ui/source-init-opts.h \
	src/ui/syntax-gen.c src/ui/syntax-gen.h
