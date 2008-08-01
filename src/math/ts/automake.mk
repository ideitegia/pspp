## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/math/ts/libpspp_ts.a

src_math_ts_libpspp_ts_a_SOURCES = \
	src/math/ts/innovations.c \
	src/math/ts/innovations.h

EXTRA_DIST += src/math/ts/OChangeLog
