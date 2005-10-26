## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES = liblinreg.a

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src

AM_CFLAGS=

if cc_is_gcc
AM_CFLAGS+=-Wall -W -Wwrite-strings -Wstrict-prototypes \
-Wpointer-arith -Wno-sign-compare -Wmissing-prototypes \
-ansi 
endif

liblinreg_a_SOURCES = sweep.c linreg.c pspp_linreg.h
