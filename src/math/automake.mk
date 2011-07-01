## Process this file with automake to produce Makefile.in  -*- makefile -*-


noinst_LTLIBRARIES += src/math/libpspp-math.la

src_math_libpspp_math_la_LIBADD = \
	lib/linreg/liblinreg.la \
	lib/tukey/libtukey.la

src_math_libpspp_math_la_SOURCES = \
	src/math/chart-geometry.c \
	src/math/chart-geometry.h \
	src/math/box-whisker.c src/math/box-whisker.h \
	src/math/categoricals.h \
	src/math/categoricals.c \
	src/math/covariance.c \
	src/math/covariance.h \
	src/math/correlation.c \
	src/math/correlation.h \
	src/math/extrema.c src/math/extrema.h \
	src/math/histogram.c src/math/histogram.h \
	src/math/interaction.c src/math/interaction.h \
	src/math/levene.c src/math/levene.h \
	src/math/linreg.c src/math/linreg.h \
	src/math/merge.c  src/math/merge.h \
	src/math/moments.c  src/math/moments.h \
	src/math/np.c src/math/np.h \
	src/math/order-stats.c src/math/order-stats.h \
	src/math/percentiles.c src/math/percentiles.h \
	src/math/random.c src/math/random.h \
        src/math/statistic.h \
	src/math/sort.c src/math/sort.h \
	src/math/trimmed-mean.c src/math/trimmed-mean.h \
	src/math/tukey-hinges.c src/math/tukey-hinges.h \
	src/math/wilcoxon-sig.c src/math/wilcoxon-sig.h
