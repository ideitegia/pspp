## Process this file with automake to produce Makefile.in  -*- makefile -*-


src_language_utilities_built_sources = \
	src/language/utilities/set.c

all_q_sources += $(src_language_utilities_built_sources:.c=.q)


EXTRA_DIST += $(src_language_utilities_built_sources:.c=.q)
nodist_src_language_utilities_libutilities_a_SOURCES = $(src_language_utilities_built_sources)
CLEANFILES += $(src_language_utilities_built_sources)

noinst_LIBRARIES += src/language/utilities/libutilities.a

src_language_utilities_libutilities_a_SOURCES = \
	src/language/utilities/date.c \
	src/language/utilities/echo.c \
	src/language/utilities/title.c \
	src/language/utilities/include.c \
	src/language/utilities/permissions.c 
