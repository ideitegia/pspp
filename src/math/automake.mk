## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/math/ts/automake.mk

noinst_LIBRARIES += src/math/libpspp_math.a

src_math_libpspp_math_a_SOURCES = \
	src/math/factor-stats.c \
	src/math/factor-stats.h \
	src/math/chart-geometry.c \
	src/math/chart-geometry.h \
	src/math/coefficient.c \
	src/math/coefficient.h \
	src/math/covariance-matrix.c \
	src/math/covariance-matrix.h \
	src/math/group.c  src/math/group.h \
	src/math/group-proc.h \
	src/math/histogram.c src/math/histogram.h \
	src/math/interaction.c \
	src/math/interaction.h \
	src/math/levene.c \
	src/math/levene.h \
	src/math/linreg.c \
	src/math/linreg.h \
	src/math/merge.c \
	src/math/merge.h \
	src/math/moments.c  src/math/moments.h \
	src/math/percentiles.c src/math/percentiles.h \
	src/math/design-matrix.c src/math/design-matrix.h \
	src/math/random.c src/math/random.h \
	src/math/sort.c src/math/sort.h 

EXTRA_DIST += src/math/OChangeLog
