## Process this file with automake to produce Makefile.in  -*- makefile -*-

bin_PROGRAMS += src/ui/gui/psppire

src_ui_gui_psppire_CFLAGS = $(GTK_CFLAGS) $(GLADE_CFLAGS) -Wall

src_ui_gui_psppire_LDFLAGS = \
	-export-dynamic 

src_ui_gui_psppire_LDADD = \
	$(GTK_LIBS) \
	$(GLADE_LIBS) \
	$(top_builddir)/lib/gtksheet/libgtksheet.a \
	$(top_builddir)/src/math/libpspp_math.a \
	$(top_builddir)/src/data/libdata.a \
	$(top_builddir)/src/libpspp/libpspp.a \
	$(top_builddir)/gl/libgl.a \
	@LIBINTL@ @LIBREADLINE@

src_ui_gui_psppiredir = $(pkgdatadir)

dist_src_ui_gui_psppire_DATA = \
	$(top_srcdir)/src/ui/gui/psppire.glade \
	$(top_srcdir)/src/ui/gui/psppicon.png \
	$(top_srcdir)/src/ui/gui/pspplogo.png \
	$(top_srcdir)/src/ui/gui/icons/value-labels.png \
	$(top_srcdir)/src/ui/gui/icons/goto-variable.png\
	$(top_srcdir)/src/ui/gui/icons/insert-case.png \
	$(top_srcdir)/src/ui/gui/icons/split-file.png \
	$(top_srcdir)/src/ui/gui/icons/select-cases.png \
	$(top_srcdir)/src/ui/gui/icons/weight-cases.png


src_ui_gui_psppire_SOURCES = \
	src/ui/gui/customentry.c \
	src/ui/gui/customentry.h \
	src/ui/gui/data-sheet.c \
	src/ui/gui/data-sheet.h \
	src/ui/gui/message-dialog.c \
	src/ui/gui/message-dialog.h \
	src/ui/gui/psppire.c \
	src/ui/gui/menu-actions.c \
	src/ui/gui/menu-actions.h \
	src/ui/gui/helper.c \
	src/ui/gui/helper.h \
	src/ui/gui/missing-val-dialog.c \
	src/ui/gui/missing-val-dialog.h \
	src/ui/gui/psppire-case-file.c \
	src/ui/gui/psppire-case-file.h \
	src/ui/gui/psppire-data-store.c \
	src/ui/gui/psppire-data-store.h \
	src/ui/gui/psppire-dict.c \
	src/ui/gui/psppire-dict.h \
	src/ui/gui/psppire-object.c \
	src/ui/gui/psppire-object.h \
	src/ui/gui/psppire-variable.c \
	src/ui/gui/psppire-variable.h \
	src/ui/gui/psppire-var-store.c \
	src/ui/gui/psppire-var-store.h \
	src/ui/gui/sort-cases-dialog.c \
	src/ui/gui/sort-cases-dialog.h \
	src/ui/gui/val-labs-dialog.c \
	src/ui/gui/val-labs-dialog.h \
	src/ui/gui/var-sheet.c \
	src/ui/gui/var-sheet.h \
	src/ui/gui/var-type-dialog.c \
	src/ui/gui/var-type-dialog.h
