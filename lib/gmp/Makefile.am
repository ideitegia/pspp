## Process this file with automake to produce Makefile.in  -*- makefile -*-

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_srcdir)/src -I$(top_srcdir)/intl

SUBDIRS = mpn mpf

noinst_LIBRARIES = libgmp.a
libgmp_a_SOURCES = extract-dbl.c gmp-mparam.h longlong.h gmp-impl.h	\
gmp.h memory.c mp_clz_tab.c

MAINTAINERCLEANFILES = Makefile.in
