## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/gtksheet/libgtksheet.a

lib/gtksheet/%: AM_CFLAGS = $(GTK_CFLAGS) -Wall


lib_gtksheet_libgtksheet_a_SOURCES = \
	lib/gtksheet/gsheet-column-iface.c \
	lib/gtksheet/gsheet-column-iface.h \
	lib/gtksheet/gsheet-hetero-column.c \
	lib/gtksheet/gsheet-hetero-column.h \
	lib/gtksheet/gsheetmodel.c \
	lib/gtksheet/gsheetmodel.h \
	lib/gtksheet/gsheet-row-iface.c \
	lib/gtksheet/gsheet-row-iface.h \
	lib/gtksheet/gsheet-uniform-column.c \
	lib/gtksheet/gsheet-uniform-column.h \
	lib/gtksheet/gsheet-uniform-row.c \
	lib/gtksheet/gsheet-uniform-row.h \
	lib/gtksheet/gtkextra.c \
	lib/gtksheet/gtkextrafeatures.h \
	lib/gtksheet/gtkextra-marshal.c \
	lib/gtksheet/gtkextra-marshal.h \
	lib/gtksheet/gtkextra-sheet.h \
	lib/gtksheet/gtkiconlist.c \
	lib/gtksheet/gtkiconlist.h \
	lib/gtksheet/gtkitementry.c \
	lib/gtksheet/gtkitementry.h \
	lib/gtksheet/gtksheet.c \
	lib/gtksheet/gtksheet.h 
