## Process this file with automake to produce Makefile.in  -*- makefile -*-


src/language/control/%: AM_CPPFLAGS += -I$(top_srcdir)/src/libpspp \
 -I$(top_srcdir)/src/data \
 -I$(top_srcdir)/src/language \
 -I$(top_srcdir)/src/language/lexer

noinst_LIBRARIES += src/language/control/libcontrol.a

src_language_control_libcontrol_a_SOURCES = \
	src/language/control/control-stack.c \
	src/language/control/control-stack.h \
	src/language/control/do-if.c \
	src/language/control/loop.c \
	src/language/control/temporary.c \
	src/language/control/repeat.c \
	src/language/control/repeat.h

