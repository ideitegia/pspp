## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/language/data-io/libdata_io.a

src_language_data_io_libdata_io_a_SOURCES = \
	src/language/data-io/data-list.c \
	src/language/data-io/get.c \
	src/language/data-io/inpt-pgm.c \
	src/language/data-io/inpt-pgm.h \
	src/language/data-io/print.c \
	src/language/data-io/print-space.c \
	src/language/data-io/matrix-data.c   \
	src/language/data-io/data-reader.c \
	src/language/data-io/data-reader.h \
	src/language/data-io/data-writer.c \
	src/language/data-io/data-writer.h \
	src/language/data-io/file-handle.h \
	src/language/data-io/placement-parser.c \
	src/language/data-io/placement-parser.h

src_language_data_io_built_sources = \
	src/language/data-io/file-handle.c \
	src/language/data-io/list.c

all_q_sources += $(src_language_data_io_built_sources:.c=.q)
EXTRA_DIST += $(src_language_data_io_built_sources:.c=.q)

nodist_src_language_data_io_libdata_io_a_SOURCES = \
	$(src_language_data_io_built_sources)


CLEANFILES += $(src_language_data_io_built_sources)

