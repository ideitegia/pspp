## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES = libmisc.a

INCLUDES = -I$(srcdir) -I$(top_srcdir)/src -I$(top_srcdir) \
-I$(top_srcdir)/intl

libmisc_a_SOURCES =
libmisc_a_LIBADD = $(ALLOCA) $(LIBOBJS)

EXTRA_DIST = getopt.c getopt1.c memcpy.c strstr.c

MAINTAINERCLEANFILES = Makefile.in
