## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES = libgsl-extras.a

AM_CPPFLAGS = -I$(top_srcdir)

AM_CFLAGS=

if cc_is_gcc
AM_CFLAGS+=-Wall -W -Wwrite-strings -Wstrict-prototypes \
-Wpointer-arith -Wno-sign-compare -Wmissing-prototypes \
-ansi 
endif

libgsl_extras_a_SOURCES = betadistinv.c binomial.c geometric.c	\
hypergeometric.c negbinom.c poisson.c gsl-extras.h

EXTRA_DIST = README
