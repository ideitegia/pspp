## Process this file with automake to produce Makefile.in  -*- makefile -*-

bin_PROGRAMS += src/ui/gui/psppire

src/ui/gui/%: AM_CPPFLAGS += \
	-export-dynamic \
	-I$(top_srcdir)/gl \
	-I$(top_srcdir)/lib/gtksheet \
	-I$(top_srcdir)/src/libpspp \
	-I$(top_srcdir)/src/data \
	-I$(top_srcdir)/src/ui/gui 


src/ui/gui/%: AM_CFLAGS = $(GTK_CFLAGS) $(GLADE_CFLAGS) -Wall

src_ui_gui_psppire_LDFLAGS = \
	-export-dynamic 

src_ui_gui_psppire_LDADD = \
	$(GTK_LIBS) \
	$(GLADE_LIBS) \
	-lgtksheet -L$(top_builddir)/lib/gtksheet \
	-ldata -L$(top_builddir)/src/data \
	-lpspp -L$(top_builddir)/src/libpspp \
	 -lgl -L$(top_builddir)/gl    \
	 @LIBINTL@ @LIBREADLINE@


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
	src/ui/gui/psppire-case-array.c \
	src/ui/gui/psppire-case-array.h \
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
	src/ui/gui/val-labs-dialog.c \
	src/ui/gui/val-labs-dialog.h \
	src/ui/gui/var-sheet.c \
	src/ui/gui/var-sheet.h \
	src/ui/gui/var-type-dialog.c \
	src/ui/gui/var-type-dialog.h 



