## Process this file with automake to produce Makefile.in  -*- makefile -*-


src/language/data-io/%: AM_CPPFLAGS += -I$(top_srcdir)/src/libpspp  \
    -I$(top_srcdir)/src/data \
    -I$(top_srcdir)/src/language \
    -I$(top_srcdir)/src/language/lexer \
    -I$(top_srcdir)/src/language/data-io \
    -I$(top_srcdir)/src/output/charts \
    -I$(top_srcdir)/src/output 

noinst_LIBRARIES += src/language/data-io/libdata_io.a

src_language_data_io_q_sources_q =  src/language/data-io/file-handle.q src/language/data-io/list.q

src_language_data_io_q_sources_c =  src/language/data-io/file-handle.c src/language/data-io/list.c

EXTRA_DIST += $(src_language_data_io_q_sources_q)
nodist_src_language_data_io_libdata_io_a_SOURCES = $(src_language_data_io_q_sources_c)
CLEANFILES += $(src_language_data_io_q_sources_c)

src_language_data_io_libdata_io_a_SOURCES = \
	src/language/data-io/data-list.c \
	src/language/data-io/file-type.c \
	src/language/data-io/get.c \
	src/language/data-io/inpt-pgm.c \
	src/language/data-io/print.c \
	src/language/data-io/matrix-data.c   \
	src/language/data-io/data-list.h \
	src/language/data-io/data-reader.c \
	src/language/data-io/data-reader.h \
	src/language/data-io/data-writer.c \
	src/language/data-io/data-writer.h \
	src/language/data-io/file-handle.h


