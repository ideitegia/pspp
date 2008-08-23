## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/linreg/liblinreg.a

lib_linreg_liblinreg_a_SOURCES = \
	lib/linreg/sweep.c  lib/linreg/sweep.h 

EXTRA_DIST += lib/linreg/OChangeLog
