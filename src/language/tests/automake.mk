## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/language/tests/libtests.a

src_language_tests_libtests_a_SOURCES = \
	src/language/tests/casefile-test.c \
	src/language/tests/moments-test.c \
	src/language/tests/pool-test.c 
