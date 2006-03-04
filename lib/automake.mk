## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/lib/linreg/automake.mk
include $(top_srcdir)/lib/gsl-extras/automake.mk

if WITHGUI
include $(top_srcdir)/lib/gtksheet/automake.mk
endif
