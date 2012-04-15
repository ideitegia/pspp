## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LTLIBRARIES += src/ui/terminal/libui.la

src_ui_terminal_libui_la_SOURCES = \
	src/ui/terminal/main.c \
	src/ui/terminal/terminal-opts.c \
	src/ui/terminal/terminal-opts.h	\
	src/ui/terminal/terminal-reader.c \
	src/ui/terminal/terminal-reader.h \
	src/ui/terminal/terminal.c \
	src/ui/terminal/terminal.h

src_ui_terminal_libui_la_CFLAGS = $(NCURSES_CFLAGS)

bin_PROGRAMS += src/ui/terminal/pspp

src_ui_terminal_pspp_SOURCES =

src_ui_terminal_pspp_LDADD = \
	src/ui/terminal/libui.la \
	src/ui/libuicommon.la \
	src/libpspp.la \
	src/libpspp-core.la \
	$(CAIRO_LIBS) \
	$(GSL_LIBS) \
	$(NCURSES_LIBS) \
	$(LIBREADLINE) \
	$(GSL_LIBS)


src_ui_terminal_pspp_LDFLAGS = $(PSPP_LDFLAGS) $(PG_LDFLAGS)

if RELOCATABLE_VIA_LD
src_ui_terminal_pspp_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
endif

EXTRA_DIST += src/ui/terminal/OChangeLog
