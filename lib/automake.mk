## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/lib/linreg/automake.mk
include $(top_srcdir)/lib/tukey/automake.mk

if HAVE_GUI
include $(top_srcdir)/lib/gtk-contrib/automake.mk
endif
