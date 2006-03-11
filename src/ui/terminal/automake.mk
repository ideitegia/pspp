## Process this file with automake to produce Makefile.in  -*- makefile -*-

src/ui/terminal/%: AM_CPPFLAGS += \
	-I$(top_srcdir)/src/libpspp \
	-I$(top_srcdir)/src/data \
	-I$(top_srcdir)/src/math \
	-I$(top_srcdir)/src/language \
	-I$(top_srcdir)/src/language/lexer \
	-I$(top_srcdir)/src/output/charts \
	-I$(top_srcdir)/src/output

noinst_LIBRARIES += src/ui/terminal/libui.a

src_ui_terminal_libui_a_SOURCES = \
 src/ui/terminal/command-line.c src/ui/terminal/command-line.h \
 src/ui/terminal/read-line.c src/ui/terminal/read-line.h  \
 src/ui/terminal/main.c


bin_PROGRAMS += src/ui/terminal/pspp

src_ui_terminal_pspp_SOURCES =					\
	src/message.c					\
	src/procedure.c  src/procedure.h 

src_ui_terminal_pspp_LDADD =					\
	$(top_builddir)/src/language/expressions/libexpressions.a \
	$(top_builddir)/src/language/liblanguage.a \
	$(top_builddir)/src/language/tests/libtests.a \
	$(top_builddir)/src/language/utilities/libutilities.a \
	$(top_builddir)/src/language/control/libcontrol.a \
	$(top_builddir)/src/language/stats/libstats.a \
	$(top_builddir)/src/language/xforms/libxforms.a \
	$(top_builddir)/src/language/dictionary/libcmddict.a \
	$(top_builddir)/src/language/lexer/liblexer.a \
	$(top_builddir)/src/language/data-io/libdata_io.a \
	$(top_builddir)/src/output/charts/libcharts.a \
	$(top_builddir)/src/output/liboutput.a \
	$(top_builddir)/src/math/libpspp_math.a  \
	$(top_builddir)/src/math/linreg/libpspp_linreg.a  \
	$(top_builddir)/src/ui/terminal/libui.a \
	$(top_builddir)/lib/linreg/liblinreg.a	\
	$(top_builddir)/lib/gsl-extras/libgsl-extras.a	\
	$(top_builddir)/src/data/libdata.a \
	$(top_builddir)/src/libpspp/libpspp.a \
	$(top_builddir)/gl/libgl.a	\
	@LIBINTL@ @LIBREADLINE@

