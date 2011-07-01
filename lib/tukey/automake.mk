## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LTLIBRARIES += lib/tukey/libtukey.la

lib_tukey_libtukey_la_SOURCES = \
	lib/tukey/ptukey.c \
	lib/tukey/qtukey.c \
	lib/tukey/tukey.h

EXTRA_DIST += lib/tukey/README 

