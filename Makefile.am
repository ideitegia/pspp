## Process this file with automake to produce Makefile.in  -*- makefile -*-

AUTOMAKE_OPTIONS = gnits 1.7 subdir-objects

SUBDIRS = intl gl po
DISTCLEANFILES = ./intl/plural.c ./po/stamp-po
BUILT_SOURCES =

AM_CPPFLAGS=-I$(top_srcdir)/intl -I$(top_srcdir)/gl -I$(top_builddir)/gl

AM_CFLAGS=

if cc_is_gcc
AM_CFLAGS+=-Wall -W -Wwrite-strings -Wstrict-prototypes \
-Wpointer-arith -Wno-sign-compare -Wmissing-prototypes
endif

if unix
AM_CFLAGS+=-Dunix
endif

if msdos
AM_CFLAGS+=-D__MSDOS__
endif

.q.c:
	@$(top_srcdir)/mkinstalldirs  `dirname $@`
	./src/language/lexer/q2c $< $@

$(all_q_sources:.q=.c): src/language/lexer/q2c$(EXEEXT)
all_q_sources =

pkgsysconfdir = $(sysconfdir)/@PACKAGE@


EXTRA_DIST = ONEWS config.rpath pspp-mode.el

CLEANFILES = 
ACLOCAL_AMFLAGS = -I m4 -I gl/m4
noinst_LIBRARIES=
noinst_PROGRAMS=
check_PROGRAMS=
bin_PROGRAMS=

include $(top_srcdir)/lib/automake.mk
include $(top_srcdir)/doc/automake.mk
include $(top_srcdir)/config/automake.mk
include $(top_srcdir)/examples/automake.mk
include $(top_srcdir)/src/automake.mk

include $(top_srcdir)/tests/automake.mk
