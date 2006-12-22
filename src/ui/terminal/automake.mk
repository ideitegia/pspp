## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += src/ui/terminal/libui.a

src_ui_terminal_libui_a_SOURCES = \
	src/ui/terminal/command-line.c \
	src/ui/terminal/command-line.h \
	src/ui/terminal/read-line.c \
	src/ui/terminal/read-line.h \
	src/ui/terminal/main.c \
	src/ui/terminal/msg-ui.c \
	src/ui/terminal/msg-ui.h


bin_PROGRAMS += src/ui/terminal/pspp

src_ui_terminal_pspp_SOURCES =

src_ui_terminal_pspp_LDADD =					\
	src/ui/terminal/libui.a \
	src/language/liblanguage.a \
	src/output/charts/libcharts.a \
	src/output/liboutput.a \
	src/math/libpspp_math.a  \
	src/math/linreg/libpspp_linreg.a  \
	src/ui/libuicommon.a \
	lib/linreg/liblinreg.a	\
	lib/gsl-extras/libgsl-extras.a	\
	src/data/libdata.a \
	src/libpspp/libpspp.a \
	gl/libgl.a	\
	$(LIBICONV) \
	@LIBINTL@ @LIBREADLINE@

