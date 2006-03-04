## Process this file with automake to produce Makefile.in  -*- makefile -*-

src/ui/terminal/%: AM_CPPFLAGS += \
	-I$(top_srcdir)/src/libpspp \
	-I$(top_srcdir)/src/data \
	-I$(top_srcdir)/src/math \
	-I$(top_srcdir)/src/language \
	-I$(top_srcdir)/src/language/lexer \
	-I$(top_srcdir)/src/output/charts \
	-I$(top_srcdir)/src/output

noinst_LIBRARIES += src/ui/terminal/libui.a

src_ui_terminal_libui_a_SOURCES = \
 src/ui/terminal/command-line.c src/ui/terminal/command-line.h \
 src/ui/terminal/read-line.c src/ui/terminal/read-line.h  \
 src/ui/terminal/main.c
