## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES = libdcdflib.a

INCLUDES = -I$(srcdir) -I$(top_srcdir)/src -I$(top_srcdir) \
-I$(top_srcdir)/intl

libdcdflib_a_SOURCES = dcdflib.c ipmpar.c
noinst_HEADERS = cdflib.h

MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = COPYING
