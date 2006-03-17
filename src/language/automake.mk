## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/language/lexer/automake.mk
include $(top_srcdir)/src/language/xforms/automake.mk
include $(top_srcdir)/src/language/control/automake.mk
include $(top_srcdir)/src/language/dictionary/automake.mk
include $(top_srcdir)/src/language/tests/automake.mk
include $(top_srcdir)/src/language/utilities/automake.mk
include $(top_srcdir)/src/language/stats/automake.mk
include $(top_srcdir)/src/language/data-io/automake.mk
include $(top_srcdir)/src/language/expressions/automake.mk

noinst_LIBRARIES += src/language/liblanguage.a 

src_language_liblanguage_a_SOURCES = \
	src/language/line-buffer.c \
	src/language/line-buffer.h \
	src/language/command.c \
	src/language/command.h \
	src/language/command.def 

