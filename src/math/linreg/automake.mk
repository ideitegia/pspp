## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/math/linreg/libpspp_linreg.a

src_math_linreg_libpspp_linreg_a_SOURCES = \
	src/math/linreg/predict.c \
	src/math/linreg/coefficient.c \
	src/math/linreg/coefficient.h \
	src/math/linreg/linreg.c \
	src/math/linreg/linreg.h 

