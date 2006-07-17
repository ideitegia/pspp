## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

include $(top_srcdir)/src/math/automake.mk
include $(top_srcdir)/src/libpspp/automake.mk
include $(top_srcdir)/src/data/automake.mk
include $(top_srcdir)/src/output/automake.mk
include $(top_srcdir)/src/language/automake.mk

include $(top_srcdir)/src/ui/automake.mk

AM_CPPFLAGS += -I$(top_srcdir)/src -I$(top_srcdir)/lib -DPKGDATADIR=\"$(pkgdatadir)\"
