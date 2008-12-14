## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/gtksheet/libgtksheet.a

lib_gtksheet_libgtksheet_a_CFLAGS = $(GTK_CFLAGS) -Wall -DGDK_MULTIHEAD_SAFE=1

lib_gtksheet_libgtksheet_a_SOURCES = \
	lib/gtksheet/psppire-sheetmodel.c \
	lib/gtksheet/psppire-sheetmodel.h \
	lib/gtksheet/gtkextra-sheet.h \
	lib/gtksheet/psppire-sheet.c \
	lib/gtksheet/psppire-sheet.h \
	lib/gtksheet/gtkxpaned.c \
	lib/gtksheet/gtkxpaned.h \
	lib/gtksheet/psppire-axis.c \
	lib/gtksheet/psppire-axis.h \
	lib/gtksheet/psppire-axis-impl.c \
	lib/gtksheet/psppire-axis-impl.h 


EXTRA_DIST += lib/gtksheet/OChangeLog \
	lib/gtksheet/README

