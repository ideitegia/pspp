## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/ui/terminal/libui.a

src_ui_terminal_libui_a_SOURCES = \
	src/ui/terminal/command-line.c \
	src/ui/terminal/command-line.h \
	src/ui/terminal/read-line.c \
	src/ui/terminal/read-line.h \
	src/ui/terminal/main.c \
	src/ui/terminal/msg-ui.c \
	src/ui/terminal/msg-ui.h \
	src/ui/terminal/terminal.c \
	src/ui/terminal/terminal.h	

src_ui_terminal_libui_a_CFLAGS = -DINSTALLDIR=\"$(bindir)\" $(NCURSES_CFLAGS)

bin_PROGRAMS += src/ui/terminal/pspp


src_ui_terminal_pspp_SOURCES =

src_ui_terminal_pspp_LDADD = \
	src/ui/terminal/libui.a \
	src/language/liblanguage.a \
	src/output/charts/libcharts.a \
	src/output/liboutput.a \
	src/math/libpspp_math.a  \
	src/ui/libuicommon.a \
	lib/linreg/liblinreg.a	\
	src/data/libdata.a \
	src/libpspp/libpspp.a \
	$(LIBXML2_LIBS) \
	$(PG_LIBS) \
	$(NCURSES_LIBS) \
	$(LIBICONV) \
	gl/libgl.la \
	@LIBINTL@ @LIBREADLINE@




src_ui_terminal_pspp_LDFLAGS = $(PSPP_LDFLAGS) $(PG_LDFLAGS)

if RELOCATABLE_VIA_LD
src_ui_terminal_pspp_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
endif

EXTRA_DIST += src/ui/terminal/OChangeLog
