## Process this file with automake to produce Makefile.in  -*- makefile -*-

src/language/tests/%: AM_CPPFLAGS += -I$(top_srcdir)/src/libpspp \
	 -I$(top_srcdir)/src/data \
	 -I$(top_srcdir)/src/math \
	 -I$(top_srcdir)/src/language \
	 -I$(top_srcdir)/src/language/lexer \
	 -I$(top_srcdir)/src/output 


noinst_LIBRARIES += src/language/tests/libtests.a

src_language_tests_libtests_a_SOURCES = \
	src/language/tests/casefile-test.c \
	src/language/tests/moments-test.c \
	src/language/tests/pool-test.c 
