## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/gtksheet/libgtksheet.a

lib_gtksheet_libgtksheet_a_CFLAGS = $(GTK_CFLAGS) -Wall -DGDK_MULTIHEAD_SAFE=1

lib_gtksheet_libgtksheet_a_SOURCES = \
	lib/gtksheet/gtkextra-sheet.h \
	lib/gtksheet/psppire-sheet.c \
	lib/gtksheet/psppire-sheet.h \
	lib/gtksheet/gtkxpaned.c \
	lib/gtksheet/gtkxpaned.h

EXTRA_DIST += lib/gtksheet/OChangeLog \
	lib/gtksheet/README

