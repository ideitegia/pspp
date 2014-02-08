## Process this file with automake to produce Makefile.in  -*- makefile -*-

src_language_data_io_built_sources = \
	src/language/data-io/file-handle.c

language_data_io_sources = \
	src/language/data-io/combine-files.c \
	src/language/data-io/data-list.c \
	src/language/data-io/data-parser.c \
	src/language/data-io/data-parser.h \
	src/language/data-io/data-reader.c \
	src/language/data-io/data-reader.h \
	src/language/data-io/data-writer.c \
	src/language/data-io/data-writer.h \
	src/language/data-io/dataset.c \
	src/language/data-io/file-handle.h \
	src/language/data-io/get-data.c \
	src/language/data-io/get.c \
	src/language/data-io/inpt-pgm.c \
	src/language/data-io/inpt-pgm.h \
	src/language/data-io/list.c \
	src/language/data-io/placement-parser.c \
	src/language/data-io/placement-parser.h \
	src/language/data-io/print-space.c \
	src/language/data-io/print.c \
	src/language/data-io/save-translate.c \
	src/language/data-io/save.c \
	src/language/data-io/trim.c \
	src/language/data-io/trim.h

all_q_sources += $(src_language_data_io_built_sources:.c=.q)
EXTRA_DIST += $(src_language_data_io_built_sources:.c=.q)
CLEANFILES += $(src_language_data_io_built_sources)
