## Process this file with automake to produce Makefile.in  -*- makefile -*-


noinst_LIBRARIES += src/language/xforms/libxforms.a

src_language_xforms_libxforms_a_SOURCES = \
	src/language/xforms/compute.c \
	src/language/xforms/count.c \
	src/language/xforms/sample.c \
	src/language/xforms/recode.c \
	src/language/xforms/select-if.c
