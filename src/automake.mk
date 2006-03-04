## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

include $(top_srcdir)/src/ui/terminal/automake.mk
include $(top_srcdir)/src/math/automake.mk
include $(top_srcdir)/src/libpspp/automake.mk
include $(top_srcdir)/src/data/automake.mk
include $(top_srcdir)/src/output/automake.mk
include $(top_srcdir)/src/language/automake.mk

if WITHGUI
include $(top_srcdir)/src/ui/gui/automake.mk
endif


src/%: AM_CPPFLAGS += \
 -I$(top_srcdir)/src/math  \
 -I$(top_srcdir)/src/ui/terminal \
 -I$(top_srcdir)/src/libpspp \
 -I$(top_srcdir)/src/data \
 -I$(top_srcdir)/src/output \
 -I$(top_srcdir)/src/language \
 -I$(top_srcdir)/src/language/lexer \
 -I$(top_srcdir)/src/language/control


bin_PROGRAMS += src/pspp

src_pspp_SOURCES =					\
	src/message.c					\
	src/procedure.c  src/procedure.h 

src_pspp_LDADD =					\
	$(top_builddir)/src/language/expressions/libexpressions.a		\
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
	

