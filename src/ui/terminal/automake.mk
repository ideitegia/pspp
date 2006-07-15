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
	src/language/tests/libtests.a \
	src/language/utilities/libutilities.a \
	src/language/control/libcontrol.a \
	src/language/expressions/libexpressions.a \
	src/language/stats/libstats.a \
	src/language/xforms/libxforms.a \
	src/language/dictionary/libcmddict.a \
	src/language/lexer/liblexer.a \
	src/language/data-io/libdata_io.a \
	src/output/charts/libcharts.a \
	src/output/liboutput.a \
	src/math/libpspp_math.a  \
	src/math/linreg/libpspp_linreg.a  \
	lib/linreg/liblinreg.a	\
	lib/gsl-extras/libgsl-extras.a	\
	src/data/libdata.a \
	src/libpspp/libpspp.a \
	gl/libgl.a	\
	$(LIBICONV) \
	@LIBINTL@ @LIBREADLINE@

