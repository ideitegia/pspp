## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/linreg/liblinreg.a

lib/linreg/%: AM_CPPFLAGS = -I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/libpspp \
	-I$(top_srcdir)/src/data


lib_linreg_liblinreg_a_SOURCES = \
	lib/linreg/sweep.c  lib/linreg/sweep.h 
