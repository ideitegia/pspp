## Process this file with automake to produce Makefile.in  -*- makefile -*-


src_language_utilities_q_sources_q =  \
	src/language/utilities/set.q


src_language_utilities_q_sources_c =  \
	src/language/utilities/set.c

noinst_LIBRARIES += src/language/utilities/libutilities.a

EXTRA_DIST += $(src_language_utilities_q_sources_q)
nodist_src_language_utilities_libutilities_a_SOURCES = $(src_language_utilities_q_sources_c)
CLEANFILES += $(src_language_utilities_q_sources_c)

src_language_utilities_libutilities_a_SOURCES = \
	src/language/utilities/date.c \
	src/language/utilities/echo.c \
	src/language/utilities/title.c \
	src/language/utilities/include.c \
	src/language/utilities/permissions.c 
