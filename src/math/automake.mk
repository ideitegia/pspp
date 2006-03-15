## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/math/linreg/automake.mk

noinst_LIBRARIES += src/math/libpspp_math.a

src_math_libpspp_math_a_SOURCES = \
	src/math/factor-stats.c \
	src/math/factor-stats.h \
	src/math/chart-geometry.c \
	src/math/chart-geometry.h \
	src/math/group.c  src/math/group.h \
	src/math/histogram.c src/math/histogram.h \
	src/math/group-proc.h \
	src/math/levene.c \
	src/math/levene.h \
	src/math/moments.c  src/math/moments.h \
	src/math/percentiles.c src/math/percentiles.h \
	src/math/design-matrix.c src/math/design-matrix.h \
	src/math/random.c src/math/random.h \
	src/math/sort.c src/math/sort.h 
