## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/ui/terminal/automake.mk
if WITHGUI
include $(top_srcdir)/src/ui/gui/automake.mk
endif


noinst_LIBRARIES += src/ui/libuicommon.a

src_ui_libuicommon_a_SOURCES = \
	src/ui/debugger.c \
	src/ui/debugger.h \
	src/ui/syntax-gen.c \
	src/ui/syntax-gen.h
