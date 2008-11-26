## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/gtksheet/libgtksheet.a

lib_gtksheet_libgtksheet_a_CFLAGS = $(GTK_CFLAGS) -Wall -DGDK_MULTIHEAD_SAFE=1


lib_gtksheet_libgtksheet_a_SOURCES = \
	lib/gtksheet/gsheetmodel.c \
	lib/gtksheet/gsheetmodel.h \
	lib/gtksheet/gtkextra-marshal.c \
	lib/gtksheet/gtkextra-marshal.h \
	lib/gtksheet/gtkextra-sheet.h \
	lib/gtksheet/gtkmarshalers.c \
	lib/gtksheet/gtkmarshalers.h \
	lib/gtksheet/gtksheet.c \
	lib/gtksheet/gtksheet.h \
	lib/gtksheet/gtkxpaned.c \
	lib/gtksheet/gtkxpaned.h \
	lib/gtksheet/psppire-axis.c \
	lib/gtksheet/psppire-axis.h \
	lib/gtksheet/psppire-axis-hetero.c \
	lib/gtksheet/psppire-axis-hetero.h \
	lib/gtksheet/psppire-axis-uniform.c \
	lib/gtksheet/psppire-axis-uniform.h



EXTRA_DIST += lib/gtksheet/OChangeLog \
	lib/gtksheet/README
