## Process this file with automake to produce Makefile.in  -*- makefile -*-

language_tests_built_sources = \
	src/language/tests/check-model.c

language_tests_sources = \
	src/language/tests/casefile-test.c \
	src/language/tests/check-model.h \
	src/language/tests/moments-test.c \
	src/language/tests/pool-test.c \
	src/language/tests/float-format.c \
	$(language_tests_built_sources)

all_q_sources += $(language_tests_built_sources:.c=.q)
EXTRA_DIST += $(language_tests_built_sources:.c=.q)
CLEANFILES += $(language_tests_built_sources)
