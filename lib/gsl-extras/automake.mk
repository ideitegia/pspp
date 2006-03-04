## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/gsl-extras/libgsl-extras.a

lib/gsl_extras/%: AM_CPPFLAGS = -I$(top_srcdir)

lib_gsl_extras_libgsl_extras_a_SOURCES = \
	lib/gsl-extras/betadistinv.c lib/gsl-extras/binomial.c lib/gsl-extras/geometric.c	\
	lib/gsl-extras/hypergeometric.c lib/gsl-extras/negbinom.c lib/gsl-extras/poisson.c lib/gsl-extras/gsl-extras.h

EXTRA_DIST += lib/gsl-extras/README
