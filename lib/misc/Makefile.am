## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES = libmisc.a

INCLUDES = -I$(srcdir) -I$(top_srcdir)/src -I$(top_srcdir) \
-I$(top_srcdir)/intl

libmisc_a_SOURCES = qsort.c 
libmisc_a_LIBADD = @ALLOCA@ @LIBOBJS@
libmisc_a_DEPENDENCIES = @ALLOCA@ @LIBOBJS@

EXTRA_DIST = alloca.c getdelim.c getline.c memchr.c memcmp.c memcpy.c \
memmem.c memmove.c memset.c stpcpy.c strcasecmp.c strerror.c \
strncasecmp.c strpbrk.c strstr.c strtok_r.c strtol.c strtoul.c

MAINTAINERCLEANFILES = Makefile.in
