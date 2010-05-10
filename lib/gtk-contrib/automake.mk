## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/gtk-contrib/libgtksheet.a

lib_gtk_contrib_libgtksheet_a_CFLAGS = $(GTK_CFLAGS) -Wall -DGDK_MULTIHEAD_SAFE=1

lib_gtk_contrib_libgtksheet_a_SOURCES = \
	lib/gtk-contrib/gtkextra-sheet.h \
	lib/gtk-contrib/psppire-sheet.c \
	lib/gtk-contrib/psppire-sheet.h \
	lib/gtk-contrib/gtkxpaned.c \
	lib/gtk-contrib/gtkxpaned.h

EXTRA_DIST += lib/gtk-contrib/OChangeLog \
	lib/gtk-contrib/README 

