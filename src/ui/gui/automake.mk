## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/ui/gui/sheet/automake.mk

bin_PROGRAMS += src/ui/gui/psppire 

src_ui_gui_psppire_CFLAGS = $(GTK_CFLAGS) -Wall -DGDK_MULTIHEAD_SAFE=1


src_ui_gui_psppire_LDFLAGS = \
	$(PSPPIRE_LDFLAGS) \
	$(PG_LDFLAGS)


if RELOCATABLE_VIA_LD
src_ui_gui_psppire_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
else
src_ui_gui_psppire_LDFLAGS += -rpath $(pkglibdir)
endif


src_ui_gui_psppire_LDADD = \
        src/ui/gui/sheet/libsheet.la \
	lib/gtk-contrib/libgtksheet.a \
	src/ui/libuicommon.la \
	src/libpspp.la \
	src/libpspp-core.la \
	$(GTK_LIBS) \
	@LIBINTL@

src_ui_gui_psppiredir = $(pkgdatadir)


themedir = $(DESTDIR)$(datadir)/icons/hicolor
context = apps


install-icons:
	for size in 16x16 ; do \
	  $(MKDIR_P) $(themedir)/$$size/$(context) ; \
          $(INSTALL) $(top_srcdir)/src/ui/gui/psppicon.png $(themedir)/$$size/$(context) ; \
	done 
	gtk-update-icon-cache --ignore-theme-index $(themedir)

INSTALL_DATA_HOOKS += install-icons

uninstall-icons:
	for size in 16x16 ; do \
          $(RM) $(themedir)/$$size/$(context)/psppicon.png ; \
	done 
	gtk-update-icon-cache --ignore-theme-index $(themedir)

UNINSTALL_DATA_HOOKS += uninstall-icons


UI_FILES = \
	src/ui/gui/crosstabs.ui \
	src/ui/gui/descriptives.ui \
	src/ui/gui/examine.ui \
	src/ui/gui/find.ui \
	src/ui/gui/frequencies.ui \
	src/ui/gui/message-dialog.ui \
	src/ui/gui/oneway.ui \
	src/ui/gui/psppire.ui \
	src/ui/gui/rank.ui \
	src/ui/gui/recode.ui \
	src/ui/gui/regression.ui \
	src/ui/gui/reliability.ui \
	src/ui/gui/t-test.ui \
	src/ui/gui/text-data-import.ui \
	src/ui/gui/var-sheet-dialogs.ui \
	src/ui/gui/variable-info.ui

nodist_src_ui_gui_psppire_DATA = \
	$(top_builddir)/src/ui/gui/data-editor.ui \
	$(top_builddir)/src/ui/gui/output-viewer.ui \
	$(top_builddir)/src/ui/gui/syntax-editor.ui


dist_src_ui_gui_psppire_DATA = \
	$(UI_FILES) \
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
	src/ui/gui/psppire-dialog.c \
	src/ui/gui/psppire-keypad.c \
	src/ui/gui/psppire-selector.c \
	src/ui/gui/psppire-buttonbox.c \
	src/ui/gui/psppire-hbuttonbox.c \
	src/ui/gui/psppire-vbuttonbox.c \
	src/ui/gui/psppire-acr.c \
	src/ui/gui/about.c \
	src/ui/gui/about.h \
	src/ui/gui/checkbox-treeview.c \
	src/ui/gui/checkbox-treeview.h \
	src/ui/gui/comments-dialog.c \
	src/ui/gui/comments-dialog.h \
	src/ui/gui/compute-dialog.c \
	src/ui/gui/compute-dialog.h \
	src/ui/gui/crosstabs-dialog.c \
	src/ui/gui/crosstabs-dialog.h \
	src/ui/gui/customentry.c \
	src/ui/gui/customentry.h \
	src/ui/gui/descriptives-dialog.c \
	src/ui/gui/descriptives-dialog.h \
	src/ui/gui/dialog-common.c \
	src/ui/gui/dialog-common.h \
	src/ui/gui/dict-display.h \
	src/ui/gui/dict-display.c \
	src/ui/gui/examine-dialog.c \
	src/ui/gui/examine-dialog.h \
	src/ui/gui/executor.c \
	src/ui/gui/executor.h \
	src/ui/gui/find-dialog.c \
	src/ui/gui/find-dialog.h \
	src/ui/gui/frequencies-dialog.c \
	src/ui/gui/frequencies-dialog.h \
	src/ui/gui/goto-case-dialog.c \
	src/ui/gui/goto-case-dialog.h \
	src/ui/gui/helper.c \
	src/ui/gui/helper.h \
	src/ui/gui/main.c \
	src/ui/gui/message-dialog.c \
	src/ui/gui/message-dialog.h \
	src/ui/gui/missing-val-dialog.c \
	src/ui/gui/missing-val-dialog.h \
        src/ui/gui/oneway-anova-dialog.c \
        src/ui/gui/oneway-anova-dialog.h \
	src/ui/gui/psppire.c \
	src/ui/gui/psppire.h \
	src/ui/gui/psppire-acr.h \
	src/ui/gui/psppire-buttonbox.h \
	src/ui/gui/psppire-conf.c \
	src/ui/gui/psppire-conf.h \
	src/ui/gui/psppire-data-editor.c \
	src/ui/gui/psppire-data-editor.h \
	src/ui/gui/psppire-data-store.c \
	src/ui/gui/psppire-data-store.h \
	src/ui/gui/psppire-data-window.c \
	src/ui/gui/psppire-data-window.h \
	src/ui/gui/psppire-dialog.h \
	src/ui/gui/psppire-dict.c \
	src/ui/gui/psppire-dict.h \
	src/ui/gui/psppire-dictview.c \
	src/ui/gui/psppire-dictview.h \
	src/ui/gui/psppire-hbuttonbox.h \
	src/ui/gui/psppire-keypad.h \
	src/ui/gui/psppire-output-window.c \
	src/ui/gui/psppire-output-window.h \
	src/ui/gui/psppire-selector.h \
	src/ui/gui/psppire-syntax-window.c \
	src/ui/gui/psppire-syntax-window.h \
	src/ui/gui/psppire-var-ptr.c \
	src/ui/gui/psppire-var-ptr.h \
	src/ui/gui/psppire-var-sheet.c \
	src/ui/gui/psppire-var-sheet.h \
	src/ui/gui/psppire-var-store.c \
	src/ui/gui/psppire-var-store.h \
	src/ui/gui/psppire-vbuttonbox.h \
	src/ui/gui/psppire-window.c \
	src/ui/gui/psppire-window.h \
	src/ui/gui/psppire-window-register.c \
	src/ui/gui/psppire-window-register.h \
	src/ui/gui/rank-dialog.c \
	src/ui/gui/rank-dialog.h \
	src/ui/gui/recode-dialog.c \
	src/ui/gui/recode-dialog.h \
	src/ui/gui/regression-dialog.c \
	src/ui/gui/regression-dialog.h \
	src/ui/gui/reliability-dialog.c \
	src/ui/gui/reliability-dialog.h \
	src/ui/gui/select-cases-dialog.c \
	src/ui/gui/select-cases-dialog.h \
	src/ui/gui/sort-cases-dialog.c \
	src/ui/gui/sort-cases-dialog.h \
	src/ui/gui/split-file-dialog.c \
	src/ui/gui/split-file-dialog.h \
	src/ui/gui/syntax-editor-source.c \
	src/ui/gui/syntax-editor-source.h \
	src/ui/gui/text-data-import-dialog.c \
	src/ui/gui/text-data-import-dialog.h \
	src/ui/gui/transpose-dialog.c \
	src/ui/gui/transpose-dialog.h \
	src/ui/gui/t-test-independent-samples-dialog.c \
	src/ui/gui/t-test-independent-samples-dialog.h \
	src/ui/gui/t-test-one-sample.c \
	src/ui/gui/t-test-one-sample.h \
	src/ui/gui/t-test-options.c \
	src/ui/gui/t-test-options.h \
	src/ui/gui/t-test-paired-samples.c \
	src/ui/gui/t-test-paired-samples.h \
	src/ui/gui/val-labs-dialog.c \
	src/ui/gui/val-labs-dialog.h \
	src/ui/gui/var-display.c \
	src/ui/gui/var-display.h \
	src/ui/gui/variable-info-dialog.c \
	src/ui/gui/variable-info-dialog.h \
	src/ui/gui/var-type-dialog.c \
	src/ui/gui/var-type-dialog.h \
	src/ui/gui/weight-cases-dialog.c \
	src/ui/gui/weight-cases-dialog.h \
	src/ui/gui/widget-io.c \
	src/ui/gui/widget-io.h \
	src/ui/gui/widgets.c \
	src/ui/gui/widgets.h \
	src/ui/gui/data-editor.glade \
	src/ui/gui/output-viewer.glade \
	src/ui/gui/syntax-editor.glade


nodist_src_ui_gui_psppire_SOURCES = \
	src/ui/gui/psppire-marshal.c \
	src/ui/gui/psppire-marshal.h


yelp-check:
	@if ! yelp --version > /dev/null 2>&1 ; then \
		echo    ; \
		echo '    The Yelp document viewer does not seem to be installed on the system.' ; \
		echo '    If Yelp is not available at run time, then the PSPPIRE online reference' ; \
		echo '    manual will not be available.' ; \
		echo '    Yelp is available from the GNOME project.  ftp://ftp.gnome.org/pub/gnome/sources/yelp' ; \
		echo ; \
	fi

PHONY += yelp-check

AM_CPPFLAGS += -Isrc

src/ui/gui/psppire-marshal.c: src/ui/gui/marshaller-list
	echo '#include <config.h>' > $@
	glib-genmarshal --body --prefix=psppire_marshal $< >> $@

src/ui/gui/psppire-marshal.h: src/ui/gui/marshaller-list
	glib-genmarshal --header --prefix=psppire_marshal $< > $@

.glade.ui:
	$(top_srcdir)/lib/gtk-contrib/gtk-builder-convert $< $@

desktopdir = $(datadir)/applications
desktop_DATA = src/ui/gui/pspp.desktop

EXTRA_DIST += src/ui/gui/OChangeLog\
	src/ui/gui/psppicon.png \
	src/ui/gui/marshaller-list \
	$(desktop_DATA)

BUILT_SOURCES += src/ui/gui/psppire-marshal.c src/ui/gui/psppire-marshal.h
CLEANFILES += src/ui/gui/psppire-marshal.c src/ui/gui/psppire-marshal.h \
	$(nodist_src_ui_gui_psppire_DATA)


