## Process this file with automake to produce Makefile.in  -*- makefile -*-

language_tests_built_sources = \
	src/language/tests/check-model.c

language_tests_sources = \
	src/language/tests/check-model.h \
	src/language/tests/datasheet-test.c \
	src/language/tests/format-guesser-test.c \
	src/language/tests/float-format.c \
	src/language/tests/moments-test.c \
	src/language/tests/paper-size.c \
	src/language/tests/pool-test.c 

all_q_sources += $(language_tests_built_sources:.c=.q)
EXTRA_DIST += $(language_tests_built_sources:.c=.q)
CLEANFILES += $(language_tests_built_sources)

EXTRA_DIST += src/language/tests/OChangeLog
