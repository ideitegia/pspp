## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/language/dictionary/libcmddict.a

src_language_dictionary_libcmddict_a_SOURCES = \
 src/language/dictionary/apply-dictionary.c \
 src/language/dictionary/formats.c \
 src/language/dictionary/missing-values.c \
 src/language/dictionary/modify-variables.c \
 src/language/dictionary/numeric.c \
 src/language/dictionary/rename-variables.c \
 src/language/dictionary/split-file.c \
 src/language/dictionary/sys-file-info.c \
 src/language/dictionary/value-labels.c \
 src/language/dictionary/variable-label.c \
 src/language/dictionary/vector.c \
 src/language/dictionary/variable-display.c \
 src/language/dictionary/weight.c

