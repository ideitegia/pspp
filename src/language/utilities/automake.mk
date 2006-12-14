## Process this file with automake to produce Makefile.in  -*- makefile -*-


src_language_utilities_built_sources = \
	src/language/utilities/set.c
language_utilities_sources = \
	src/language/utilities/date.c \
	src/language/utilities/echo.c \
	src/language/utilities/title.c \
	src/language/utilities/include.c \
	src/language/utilities/permissions.c \
	$(src_language_utilities_built_sources)

all_q_sources += $(src_language_utilities_built_sources:.c=.q)
EXTRA_DIST += $(src_language_utilities_built_sources:.c=.q)
CLEANFILES += $(src_language_utilities_built_sources)

