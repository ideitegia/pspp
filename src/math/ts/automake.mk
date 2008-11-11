## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LTLIBRARIES += src/math/ts/libpspp_ts.la

src_math_ts_libpspp_ts_la_SOURCES = \
	src/math/ts/innovations.c \
	src/math/ts/innovations.h

EXTRA_DIST += src/math/ts/OChangeLog
