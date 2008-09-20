## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/misc/libmisc.a

lib_misc_libmisc_a_SOURCES = \
	lib/misc/wx-mp-sr.c  lib/misc/wx-mp-sr.h 

EXTRA_DIST += lib/misc/README
