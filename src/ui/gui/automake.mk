## Process this file with automake to produce Makefile.in  -*- makefile -*-

UI_FILES = \
	src/ui/gui/aggregate.ui \
	src/ui/gui/autorecode.ui \
	src/ui/gui/binomial.ui \
	src/ui/gui/compute.ui \
	src/ui/gui/correlation.ui \
	src/ui/gui/count.ui \
	src/ui/gui/crosstabs.ui \
	src/ui/gui/chi-square.ui \
	src/ui/gui/data-sheet.ui \
	src/ui/gui/descriptives.ui \
	src/ui/gui/entry-dialog.ui \
	src/ui/gui/examine.ui \
	src/ui/gui/goto-case.ui \
	src/ui/gui/factor.ui \
	src/ui/gui/find.ui \
	src/ui/gui/frequencies.ui \
	src/ui/gui/indep-samples.ui \
	src/ui/gui/k-means.ui \
	src/ui/gui/k-related.ui \
	src/ui/gui/ks-one-sample.ui \
	src/ui/gui/logistic.ui \
	src/ui/gui/means.ui \
	src/ui/gui/missing-val-dialog.ui \
	src/ui/gui/oneway.ui \
	src/ui/gui/paired-samples.ui \
	src/ui/gui/psppire.ui \
	src/ui/gui/rank.ui \
	src/ui/gui/runs.ui \
	src/ui/gui/sort.ui \
	src/ui/gui/split-file.ui \
	src/ui/gui/recode.ui \
	src/ui/gui/regression.ui \
	src/ui/gui/reliability.ui \
	src/ui/gui/roc.ui \
	src/ui/gui/select-cases.ui \
	src/ui/gui/t-test.ui \
	src/ui/gui/text-data-import.ui \
	src/ui/gui/univariate.ui \
	src/ui/gui/val-labs-dialog.ui \
	src/ui/gui/variable-info.ui \
	src/ui/gui/data-editor.ui \
	src/ui/gui/output-viewer.ui \
	src/ui/gui/syntax-editor.ui \
	src/ui/gui/var-sheet.ui \
	src/ui/gui/var-type-dialog.ui

EXTRA_DIST += \
	src/ui/gui/OChangeLog \
	src/ui/gui/marshaller-list \
	src/ui/gui/gen-dot-desktop.sh


if HAVE_GUI
bin_PROGRAMS += src/ui/gui/psppire 
noinst_PROGRAMS += src/ui/gui/spreadsheet-test

src_ui_gui_psppire_CFLAGS = $(GTK_CFLAGS) $(GTKSOURCEVIEW_CFLAGS) -Wall -DGDK_MULTIHEAD_SAFE=1
src_ui_gui_spreadsheet_test_CFLAGS = $(GTK_CFLAGS) -Wall -DGDK_MULTIHEAD_SAFE=1


src_ui_gui_psppire_LDFLAGS = \
	$(PSPPIRE_LDFLAGS) \
	$(PG_LDFLAGS)


if RELOCATABLE_VIA_LD
src_ui_gui_psppire_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
else
src_ui_gui_psppire_LDFLAGS += -rpath $(pkglibdir)
endif


src_ui_gui_psppire_LDADD = \
	lib/gtk-contrib/libxpaned.a \
	src/ui/libuicommon.la \
	src/libpspp.la \
	src/libpspp-core.la \
	$(GTK_LIBS) \
	$(GTHREAD_LIBS) \
	$(GTKSOURCEVIEW_LIBS) \
	$(CAIRO_LIBS) \
	$(LIBINTL) \
	$(GSL_LIBS)


src_ui_gui_spreadsheet_test_LDADD = \
	src/libpspp-core.la \
	$(GTK_LIBS) \
	$(GTHREAD_LIBS)


src_ui_gui_spreadsheet_test_SOURCES = src/ui/gui/spreadsheet-test.c src/ui/gui/psppire-spreadsheet-model.c


src_ui_gui_psppiredir = $(pkgdatadir)


install-lang:
	$(INSTALL_DATA) $(top_srcdir)/src/ui/gui/pspp.lang $(DESTDIR)$(pkgdatadir)

INSTALL_DATA_HOOKS += install-lang

dist_src_ui_gui_psppire_DATA = \
	$(UI_FILES) \
	$(top_srcdir)/src/ui/gui/pspp.lang \
	$(top_srcdir)/src/ui/gui/psppire.gtkrc

src_ui_gui_psppire_SOURCES = \
	src/ui/gui/pspp-sheet-private.h \
	src/ui/gui/pspp-sheet-selection.c \
	src/ui/gui/pspp-sheet-selection.h \
	src/ui/gui/pspp-sheet-view-column.c \
	src/ui/gui/pspp-sheet-view-column.h \
	src/ui/gui/pspp-sheet-view.c \
	src/ui/gui/pspp-sheet-view.h \
	src/ui/gui/pspp-widget-facade.c \
	src/ui/gui/pspp-widget-facade.h \
	src/ui/gui/psppire-button-editable.c \
	src/ui/gui/psppire-button-editable.h \
	src/ui/gui/psppire-cell-renderer-button.c \
	src/ui/gui/psppire-cell-renderer-button.h \
	src/ui/gui/psppire-dialog.c \
	src/ui/gui/psppire-keypad.c \
	src/ui/gui/psppire-selector.c \
	src/ui/gui/psppire-buttonbox.c \
	src/ui/gui/psppire-hbuttonbox.c \
	src/ui/gui/psppire-vbuttonbox.c \
	src/ui/gui/psppire-scanf.c \
	src/ui/gui/psppire-scanf.h \
	src/ui/gui/psppire-acr.c \
	src/ui/gui/autorecode-dialog.c \
	src/ui/gui/autorecode-dialog.h \
	src/ui/gui/aggregate-dialog.c \
	src/ui/gui/aggregate-dialog.h \
	src/ui/gui/builder-wrapper.c \
	src/ui/gui/builder-wrapper.h \
	src/ui/gui/comments-dialog.c \
	src/ui/gui/comments-dialog.h \
	src/ui/gui/compute-dialog.c \
	src/ui/gui/compute-dialog.h \
	src/ui/gui/count-dialog.c \
	src/ui/gui/count-dialog.h \
	src/ui/gui/dialog-common.c \
	src/ui/gui/dialog-common.h \
	src/ui/gui/dict-display.h \
	src/ui/gui/dict-display.c \
	src/ui/gui/entry-dialog.c \
	src/ui/gui/entry-dialog.h \
	src/ui/gui/executor.c \
	src/ui/gui/executor.h \
	src/ui/gui/find-dialog.c \
	src/ui/gui/find-dialog.h \
	src/ui/gui/goto-case-dialog.c \
	src/ui/gui/goto-case-dialog.h \
	src/ui/gui/helper.c \
	src/ui/gui/help-menu.c \
	src/ui/gui/help-menu.h \
	src/ui/gui/helper.h \
	src/ui/gui/k-related-dialog.c \
	src/ui/gui/k-related-dialog.h \
	src/ui/gui/ks-one-sample-dialog.c \
	src/ui/gui/ks-one-sample-dialog.h \
	src/ui/gui/main.c \
	src/ui/gui/missing-val-dialog.c \
	src/ui/gui/missing-val-dialog.h \
        src/ui/gui/oneway-anova-dialog.c \
        src/ui/gui/oneway-anova-dialog.h \
	src/ui/gui/paired-dialog.c \
	src/ui/gui/paired-dialog.h \
	src/ui/gui/psppire.c \
	src/ui/gui/psppire.h \
	src/ui/gui/psppire-acr.h \
	src/ui/gui/psppire-buttonbox.h \
	src/ui/gui/psppire-checkbox-treeview.c \
	src/ui/gui/psppire-checkbox-treeview.h \
	src/ui/gui/psppire-conf.c \
	src/ui/gui/psppire-conf.h \
	src/ui/gui/psppire-data-editor.c \
	src/ui/gui/psppire-data-editor.h \
	src/ui/gui/psppire-data-sheet.c \
	src/ui/gui/psppire-data-sheet.h \
	src/ui/gui/psppire-data-store.c \
	src/ui/gui/psppire-data-store.h \
	src/ui/gui/psppire-data-window.c \
	src/ui/gui/psppire-data-window.h \
	src/ui/gui/psppire-dialog.h \
	src/ui/gui/psppire-dialog-action.c \
	src/ui/gui/psppire-dialog-action.h \
	src/ui/gui/psppire-dialog-action-binomial.c \
	src/ui/gui/psppire-dialog-action-binomial.h \
	src/ui/gui/psppire-dialog-action-chisquare.c \
	src/ui/gui/psppire-dialog-action-chisquare.h \
	src/ui/gui/psppire-dialog-action-correlation.c \
	src/ui/gui/psppire-dialog-action-correlation.h \
	src/ui/gui/psppire-dialog-action-crosstabs.c \
	src/ui/gui/psppire-dialog-action-crosstabs.h \
	src/ui/gui/psppire-dialog-action-descriptives.c \
	src/ui/gui/psppire-dialog-action-descriptives.h \
	src/ui/gui/psppire-dialog-action-examine.c \
	src/ui/gui/psppire-dialog-action-examine.h \
	src/ui/gui/psppire-dialog-action-factor.c \
	src/ui/gui/psppire-dialog-action-factor.h \
	src/ui/gui/psppire-dialog-action-flip.c \
	src/ui/gui/psppire-dialog-action-flip.h \
	src/ui/gui/psppire-dialog-action-frequencies.c \
	src/ui/gui/psppire-dialog-action-frequencies.h \
	src/ui/gui/psppire-dialog-action-indep-samps.c \
	src/ui/gui/psppire-dialog-action-indep-samps.h \
	src/ui/gui/psppire-dialog-action-kmeans.c \
	src/ui/gui/psppire-dialog-action-kmeans.h \
	src/ui/gui/psppire-dialog-action-logistic.c \
	src/ui/gui/psppire-dialog-action-logistic.h \
	src/ui/gui/psppire-dialog-action-means.c \
	src/ui/gui/psppire-dialog-action-means.h \
	src/ui/gui/psppire-dialog-action-rank.c \
	src/ui/gui/psppire-dialog-action-rank.h \
	src/ui/gui/psppire-dialog-action-regression.c \
	src/ui/gui/psppire-dialog-action-regression.h \
	src/ui/gui/psppire-dialog-action-reliability.c \
	src/ui/gui/psppire-dialog-action-reliability.h \
	src/ui/gui/psppire-dialog-action-roc.c \
	src/ui/gui/psppire-dialog-action-roc.h \
	src/ui/gui/psppire-dialog-action-runs.c \
	src/ui/gui/psppire-dialog-action-runs.h \
	src/ui/gui/psppire-dialog-action-sort.c \
	src/ui/gui/psppire-dialog-action-sort.h \
	src/ui/gui/psppire-dialog-action-univariate.c \
	src/ui/gui/psppire-dialog-action-univariate.h \
	src/ui/gui/psppire-dialog-action-var-info.c \
	src/ui/gui/psppire-dialog-action-var-info.h \
	src/ui/gui/psppire-dict.c \
	src/ui/gui/psppire-dict.h \
	src/ui/gui/psppire-dictview.c \
	src/ui/gui/psppire-dictview.h \
	src/ui/gui/psppire-empty-list-store.c \
	src/ui/gui/psppire-empty-list-store.h \
	src/ui/gui/psppire-encoding-selector.c \
	src/ui/gui/psppire-encoding-selector.h \
	src/ui/gui/psppire-format.c \
	src/ui/gui/psppire-format.h \
	src/ui/gui/psppire-hbuttonbox.h \
	src/ui/gui/psppire-keypad.h \
	src/ui/gui/psppire-lex-reader.c \
	src/ui/gui/psppire-lex-reader.h \
	src/ui/gui/psppire-means-layer.c \
	src/ui/gui/psppire-means-layer.h \
	src/ui/gui/psppire-output-window.c \
	src/ui/gui/psppire-output-window.h \
	src/ui/gui/psppire-var-view.c \
	src/ui/gui/psppire-var-view.h \
	src/ui/gui/psppire-spreadsheet-model.c \
	src/ui/gui/psppire-spreadsheet-model.h \
	src/ui/gui/psppire-selector.h \
	src/ui/gui/psppire-select-dest.c \
	src/ui/gui/psppire-select-dest.h \
	src/ui/gui/psppire-syntax-window.c \
	src/ui/gui/psppire-syntax-window.h \
	src/ui/gui/psppire-val-chooser.c \
	src/ui/gui/psppire-val-chooser.h \
	src/ui/gui/psppire-value-entry.c \
	src/ui/gui/psppire-value-entry.h \
	src/ui/gui/psppire-var-ptr.c \
	src/ui/gui/psppire-var-ptr.h \
	src/ui/gui/psppire-var-sheet.c \
	src/ui/gui/psppire-var-sheet.h \
	src/ui/gui/psppire-vbuttonbox.h \
	src/ui/gui/psppire-window.c \
	src/ui/gui/psppire-window.h \
	src/ui/gui/psppire-window-base.c \
	src/ui/gui/psppire-window-base.h \
	src/ui/gui/psppire-window-register.c \
	src/ui/gui/psppire-window-register.h \
	src/ui/gui/recode-dialog.c \
	src/ui/gui/recode-dialog.h \
	src/ui/gui/select-cases-dialog.c \
	src/ui/gui/select-cases-dialog.h \
	src/ui/gui/split-file-dialog.c \
	src/ui/gui/split-file-dialog.h \
	src/ui/gui/page-assistant.c \
	src/ui/gui/page-intro.c \
	src/ui/gui/page-intro.h \
	src/ui/gui/page-file.c \
	src/ui/gui/page-first-line.c \
	src/ui/gui/page-first-line.h \
	src/ui/gui/page-formats.c \
	src/ui/gui/page-formats.h \
	src/ui/gui/page-separators.c \
	src/ui/gui/page-separators.h \
	src/ui/gui/page-sheet-spec.c \
	src/ui/gui/page-sheet-spec.h \
	src/ui/gui/text-data-import-dialog.c \
	src/ui/gui/text-data-import-dialog.h \
	src/ui/gui/t-test-one-sample.c \
	src/ui/gui/t-test-one-sample.h \
	src/ui/gui/t-test-options.c \
	src/ui/gui/t-test-options.h \
	src/ui/gui/t-test-paired-samples.c \
	src/ui/gui/t-test-paired-samples.h \
	src/ui/gui/npar-two-sample-related.c \
	src/ui/gui/npar-two-sample-related.h \
	src/ui/gui/val-labs-dialog.c \
	src/ui/gui/val-labs-dialog.h \
	src/ui/gui/var-display.c \
	src/ui/gui/var-display.h \
	src/ui/gui/var-type-dialog.c \
	src/ui/gui/var-type-dialog.h \
	src/ui/gui/weight-cases-dialog.c \
	src/ui/gui/weight-cases-dialog.h \
	src/ui/gui/widget-io.c \
	src/ui/gui/widget-io.h \
	src/ui/gui/widgets.c \
	src/ui/gui/widgets.h

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

src/ui/gui/pspp.desktop: src/ui/gui/gen-dot-desktop.sh $(POFILES)
	POFILES="$(POFILES)" top_builddir="$(top_builddir)" $(SHELL) $< > $@

CLEANFILES+=src/ui/gui/pspp.desktop

src/ui/gui/psppire-marshal.c: src/ui/gui/marshaller-list
	echo '#include <config.h>' > $@
	$(GLIB_GENMARSHAL) --body --prefix=psppire_marshal $? >> $@

src/ui/gui/psppire-marshal.h: src/ui/gui/marshaller-list
	$(GLIB_GENMARSHAL) --header --prefix=psppire_marshal $? > $@

desktopdir = $(datadir)/applications
desktop_DATA = src/ui/gui/pspp.desktop

BUILT_SOURCES += src/ui/gui/psppire-marshal.c src/ui/gui/psppire-marshal.h
CLEANFILES += src/ui/gui/psppire-marshal.c src/ui/gui/psppire-marshal.h \
	$(nodist_src_ui_gui_psppire_DATA)
endif HAVE_GUI

#ensure the installcheck passes even if there is no X server available
installcheck-local:
	DISPLAY=/invalid/port $(MAKE) $(AM_MAKEFLAGS) installcheck-binPROGRAMS

# <gtk/gtk.h> wrapper
src_ui_gui_psppire_CPPFLAGS = $(AM_CPPFLAGS) -Isrc/ui/gui/include
BUILT_SOURCES += src/ui/gui/include/gtk/gtk.h
src/ui/gui/include/gtk/gtk.h: src/ui/gui/include/gtk/gtk.in.h
	@$(MKDIR_P) src/ui/gui/include/gtk
	$(AM_V_GEN)rm -f $@-t $@ && \
	{ echo '/* DO NOT EDIT! GENERATED AUTOMATICALLY! */'; \
	  sed -e 's|@''INCLUDE_NEXT''@|$(INCLUDE_NEXT)|g' \
	      -e 's|@''PRAGMA_SYSTEM_HEADER''@|@PRAGMA_SYSTEM_HEADER@|g' \
	      -e 's|@''PRAGMA_COLUMNS''@|@PRAGMA_COLUMNS@|g' \
	      -e 's|@''NEXT_GTK_GTK_H''@|$(NEXT_GTK_GTK_H)|g' \
	      < $(srcdir)/src/ui/gui/include/gtk/gtk.in.h; \
	} > $@-t && \
	mv $@-t $@
CLEANFILES += src/ui/gui/include/gtk/gtk.h
EXTRA_DIST += src/ui/gui/include/gtk/gtk.in.h

include $(top_srcdir)/src/ui/gui/icons/automake.mk

UNINSTALL_DATA_HOOKS += update-icon-cache
INSTALL_DATA_HOOKS += update-icon-cache

