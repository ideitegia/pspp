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


src/message.o: AM_CPPFLAGS += \
 -I$(top_srcdir)/src/language \
 -I$(top_srcdir)/src/language/lexer \
 -I$(top_srcdir)/src/ui/terminal \
 -I$(top_srcdir)/src/data \
 -I$(top_srcdir)/src/libpspp

src/procedure.o: AM_CPPFLAGS += \
 -I$(top_srcdir)/src/language \
 -I$(top_srcdir)/src/language/control \
 -I$(top_srcdir)/src/output \
 -I$(top_srcdir)/src/data \
 -I$(top_srcdir)/src/libpspp


