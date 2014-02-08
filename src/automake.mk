## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

include $(top_srcdir)/src/libpspp/automake.mk
include $(top_srcdir)/src/data/automake.mk



AM_CPPFLAGS += -I$(top_srcdir)/src -I$(top_srcdir)/lib


pkglib_LTLIBRARIES = src/libpspp-core.la src/libpspp.la
src_libpspp_core_la_SOURCES = 


src_libpspp_core_la_LDFLAGS = -release $(VERSION)

src_libpspp_core_la_LIBADD = \
	src/data/libdata.la \
	src/libpspp/liblibpspp.la \
	$(LIBXML2_LIBS) $(PG_LIBS) \
	gl/libgl.la

src_libpspp_la_SOURCES = 

src_libpspp_la_CFLAGS = $(GSL_CFLAGS)
src_libpspp_la_LDFLAGS = -release $(VERSION)

src_libpspp_la_LIBADD = \
	src/language/liblanguage.la \
	src/math/libpspp-math.la \
	src/output/liboutput.la \
        $(GSL_LIBS)

include $(top_srcdir)/src/math/automake.mk
include $(top_srcdir)/src/output/automake.mk
include $(top_srcdir)/src/language/automake.mk
include $(top_srcdir)/src/ui/automake.mk
