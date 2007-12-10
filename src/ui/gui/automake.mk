## Process this file with automake to produce Makefile.in  -*- makefile -*-



bin_PROGRAMS += src/ui/gui/psppire 

src_ui_gui_psppire_CFLAGS = $(GTK_CFLAGS) $(GLADE_CFLAGS) -Wall \
	-DINSTALLDIR=\"$(bindir)\"


src_ui_gui_psppire_LDFLAGS = \
	-export-dynamic 


if RELOCATABLE_VIA_LD
src_ui_gui_psppire_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
else
src_ui_gui_psppire_LDFLAGS += -rpath $(pkglibdir)
endif


pkglib_LTLIBRARIES = src/ui/gui/libpsppire.la

src_ui_gui_libpsppire_la_CFLAGS = $(GLADE_CFLAGS) 

src_ui_gui_libpsppire_la_SOURCES = \
	src/ui/gui/glade-register.c

src_ui_gui_psppire_LDADD = \
	-dlopen src/ui/gui/libpsppire.la \
	lib/gtksheet/libgtksheet.a \
	src/language/liblanguage.a \
	src/ui/libuicommon.a \
	src/output/charts/libcharts.a \
	src/output/liboutput.a \
	src/math/libpspp_math.a  \
	src/math/linreg/libpspp_linreg.a  \
	lib/linreg/liblinreg.a	\
	lib/gsl-extras/libgsl-extras.a	\
	src/data/libdata.a \
	src/libpspp/libpspp.a \
	$(GTK_LIBS) \
	$(GLADE_LIBS) \
	gl/libgl.la \
	@LIBINTL@ @LIBREADLINE@

src_ui_gui_psppiredir = $(pkgdatadir)

dist_src_ui_gui_psppire_DATA = \
	$(top_srcdir)/src/ui/gui/data-editor.glade \
	$(top_srcdir)/src/ui/gui/descriptives-dialog.glade \
	$(top_srcdir)/src/ui/gui/frequencies.glade \
	$(top_srcdir)/src/ui/gui/oneway.glade \
	$(top_srcdir)/src/ui/gui/output-viewer.glade \
	$(top_srcdir)/src/ui/gui/psppire.glade \
	$(top_srcdir)/src/ui/gui/rank.glade \
	$(top_srcdir)/src/ui/gui/recode.glade \
	$(top_srcdir)/src/ui/gui/syntax-editor.glade \
	$(top_srcdir)/src/ui/gui/t-test.glade \
	$(top_srcdir)/src/ui/gui/psppicon.png \
	$(top_srcdir)/src/ui/gui/pspplogo.png \
	$(top_srcdir)/src/ui/gui/icons/value-labels.png \
	$(top_srcdir)/src/ui/gui/icons/goto-variable.png\
	$(top_srcdir)/src/ui/gui/icons/insert-case.png \
	$(top_srcdir)/src/ui/gui/icons/insert-variable.png \
	$(top_srcdir)/src/ui/gui/icons/recent-dialogs.png \
	$(top_srcdir)/src/ui/gui/icons/split-file.png \
	$(top_srcdir)/src/ui/gui/icons/select-cases.png \
	$(top_srcdir)/src/ui/gui/icons/weight-cases.png \
	$(top_srcdir)/src/ui/gui/icons/16x16/nominal.png  \
	$(top_srcdir)/src/ui/gui/icons/16x16/ordinal.png \
	$(top_srcdir)/src/ui/gui/icons/16x16/scale.png \
	$(top_srcdir)/src/ui/gui/icons/16x16/string.png \
	$(top_srcdir)/src/ui/gui/icons/16x16/date-scale.png \
	$(top_srcdir)/src/ui/gui/icons/splash.png


src_ui_gui_psppire_SOURCES = \
	src/ui/gui/about.c \
	src/ui/gui/about.h \
	src/ui/gui/clipboard.c \
	src/ui/gui/clipboard.h \
	src/ui/gui/checkbox-treeview.c \
	src/ui/gui/checkbox-treeview.h \
	src/ui/gui/compute-dialog.c \
	src/ui/gui/compute-dialog.h \
	src/ui/gui/comments-dialog.c \
	src/ui/gui/comments-dialog.h \
	src/ui/gui/customentry.c \
	src/ui/gui/customentry.h \
	src/ui/gui/frequencies-dialog.c \
	src/ui/gui/frequencies-dialog.h \
	src/ui/gui/goto-case-dialog.c \
	src/ui/gui/goto-case-dialog.h \
	src/ui/gui/data-sheet.c \
	src/ui/gui/data-sheet.h \
	src/ui/gui/data-editor.c \
	src/ui/gui/data-editor.h \
	src/ui/gui/descriptives-dialog.c \
	src/ui/gui/descriptives-dialog.h \
	src/ui/gui/find-dialog.c \
	src/ui/gui/find-dialog.h \
	src/ui/gui/dialog-common.c \
	src/ui/gui/dialog-common.h \
	src/ui/gui/dict-display.c \
	src/ui/gui/dict-display.h \
	src/ui/gui/main.c \
	src/ui/gui/message-dialog.c \
	src/ui/gui/message-dialog.h \
	src/ui/gui/psppire.c \
	src/ui/gui/psppire.h \
	src/ui/gui/helper.c \
	src/ui/gui/helper.h \
	src/ui/gui/missing-val-dialog.c \
	src/ui/gui/missing-val-dialog.h \
        src/ui/gui/oneway-anova-dialog.c \
        src/ui/gui/oneway-anova-dialog.h \
	src/ui/gui/output-viewer.c \
	src/ui/gui/output-viewer.h \
	src/ui/gui/psppire-acr.c \
	src/ui/gui/psppire-acr.h \
	src/ui/gui/psppire-buttonbox.c \
	src/ui/gui/psppire-buttonbox.h \
	src/ui/gui/psppire-hbuttonbox.c \
	src/ui/gui/psppire-hbuttonbox.h \
	src/ui/gui/psppire-vbuttonbox.c \
	src/ui/gui/psppire-vbuttonbox.h \
	src/ui/gui/psppire-case-file.c \
	src/ui/gui/psppire-case-file.h \
	src/ui/gui/psppire-data-store.c \
	src/ui/gui/psppire-data-store.h \
	src/ui/gui/psppire-dialog.c \
	src/ui/gui/psppire-dialog.h \
	src/ui/gui/psppire-dict.c \
	src/ui/gui/psppire-dict.h \
	src/ui/gui/psppire-keypad.c \
	src/ui/gui/psppire-keypad.h \
	src/ui/gui/psppire-selector.c \
	src/ui/gui/psppire-selector.h \
	src/ui/gui/psppire-var-store.c \
	src/ui/gui/psppire-var-store.h \
	src/ui/gui/rank-dialog.c \
	src/ui/gui/rank-dialog.h \
	src/ui/gui/recode-dialog.c \
	src/ui/gui/recode-dialog.h \
	src/ui/gui/select-cases-dialog.c \
	src/ui/gui/select-cases-dialog.h \
	src/ui/gui/sort-cases-dialog.c \
	src/ui/gui/sort-cases-dialog.h \
	src/ui/gui/split-file-dialog.c \
	src/ui/gui/split-file-dialog.h \
	src/ui/gui/syntax-editor.c \
	src/ui/gui/syntax-editor.h \
	src/ui/gui/syntax-editor-source.c \
	src/ui/gui/syntax-editor-source.h \
	src/ui/gui/transpose-dialog.c \
	src/ui/gui/transpose-dialog.h \
	src/ui/gui/t-test-independent-samples-dialog.c \
	src/ui/gui/t-test-independent-samples-dialog.h \
	src/ui/gui/t-test-one-sample.c \
	src/ui/gui/t-test-one-sample.h \
	src/ui/gui/t-test-options.c \
	src/ui/gui/t-test-options.h \
	src/ui/gui/val-labs-dialog.c \
	src/ui/gui/val-labs-dialog.h \
	src/ui/gui/var-display.c \
	src/ui/gui/var-display.h \
	src/ui/gui/var-sheet.c \
	src/ui/gui/var-sheet.h \
	src/ui/gui/var-type-dialog.c \
	src/ui/gui/var-type-dialog.h \
	src/ui/gui/variable-info-dialog.c \
	src/ui/gui/variable-info-dialog.h \
	src/ui/gui/weight-cases-dialog.c \
	src/ui/gui/weight-cases-dialog.h \
	src/ui/gui/widget-io.c \
	src/ui/gui/widget-io.h \
	src/ui/gui/window-manager.c \
	src/ui/gui/window-manager.h


