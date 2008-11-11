## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LTLIBRARIES += lib/misc/libmisc.la

lib_misc_libmisc_la_SOURCES = \
	lib/misc/wx-mp-sr.c  lib/misc/wx-mp-sr.h 

EXTRA_DIST += lib/misc/README
