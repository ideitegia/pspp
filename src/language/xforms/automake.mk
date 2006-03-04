## Process this file with automake to produce Makefile.in  -*- makefile -*-


src/language/xforms/%: AM_CPPFLAGS += -I$(top_srcdir)/src/libpspp \
 -I$(top_srcdir)/src/data \
 -I$(top_srcdir)/src/math \
 -I$(top_srcdir)/src/language/lexer \
 -I$(top_srcdir)/src/language

noinst_LIBRARIES += src/language/xforms/libxforms.a

src_language_xforms_libxforms_a_SOURCES = \
	src/language/xforms/compute.c \
	src/language/xforms/count.c \
	src/language/xforms/sample.c \
	src/language/xforms/recode.c \
	src/language/xforms/select-if.c
