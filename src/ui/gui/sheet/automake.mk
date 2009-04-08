## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LTLIBRARIES += src/ui/gui/sheet/libsheet.la

src_ui_gui_sheet_libsheet_la_CFLAGS = $(GTK_CFLAGS)

src_ui_gui_sheet_libsheet_la_SOURCES = \
	src/ui/gui/sheet/psppire-axis.c \
	src/ui/gui/sheet/psppire-axis.h \
	src/ui/gui/sheet/psppire-sheetmodel.c \
	src/ui/gui/sheet/psppire-sheetmodel.h

