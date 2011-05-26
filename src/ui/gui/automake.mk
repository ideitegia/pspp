## Process this file with automake to produce Makefile.in  -*- makefile -*-

include $(top_srcdir)/src/ui/gui/sheet/automake.mk

UI_FILES = \
	src/ui/gui/aggregate.ui \
	src/ui/gui/binomial.ui \
	src/ui/gui/compute.ui \
	src/ui/gui/correlation.ui \
	src/ui/gui/crosstabs.ui \
	src/ui/gui/chi-square.ui \
	src/ui/gui/descriptives.ui \
	src/ui/gui/entry-dialog.ui \
	src/ui/gui/examine.ui \
	src/ui/gui/goto-case.ui \
	src/ui/gui/factor.ui \
	src/ui/gui/find.ui \
	src/ui/gui/frequencies.ui \
	src/ui/gui/k-related.ui \
	src/ui/gui/oneway.ui \
	src/ui/gui/psppire.ui \
	src/ui/gui/rank.ui \
	src/ui/gui/sort.ui \
	src/ui/gui/split-file.ui \
	src/ui/gui/recode.ui \
	src/ui/gui/regression.ui \
	src/ui/gui/reliability.ui \
	src/ui/gui/roc.ui \
	src/ui/gui/select-cases.ui \
	src/ui/gui/t-test.ui \
	src/ui/gui/text-data-import.ui \
	src/ui/gui/var-sheet-dialogs.ui \
	src/ui/gui/variable-info.ui \
	src/ui/gui/data-editor.ui \
	src/ui/gui/output-viewer.ui \
	src/ui/gui/syntax-editor.ui

EXTRA_DIST += \
	src/ui/gui/OChangeLog \
	src/ui/gui/psppicon.png \
	src/ui/gui/marshaller-list \
	src/ui/gui/pspp.desktop

if HAVE_GUI
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
	$(CAIRO_LIBS)

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
          rm -f $(themedir)/$$size/$(context)/psppicon.png ; \
	done 
	gtk-update-icon-cache --ignore-theme-index $(themedir)

UNINSTALL_DATA_HOOKS += uninstall-icons

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
	src/ui/gui/aggregate-dialog.c \
	src/ui/gui/aggregate-dialog.h \
	src/ui/gui/binomial-dialog.c \
	src/ui/gui/binomial-dialog.h \
	src/ui/gui/checkbox-treeview.c \
	src/ui/gui/checkbox-treeview.h \
	src/ui/gui/comments-dialog.c \
	src/ui/gui/comments-dialog.h \
	src/ui/gui/compute-dialog.c \
	src/ui/gui/compute-dialog.h \
	src/ui/gui/chi-square-dialog.c \
	src/ui/gui/chi-square-dialog.h \
	src/ui/gui/correlation-dialog.c \
	src/ui/gui/correlation-dialog.h \
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
	src/ui/gui/entry-dialog.c \
	src/ui/gui/entry-dialog.h \
	src/ui/gui/examine-dialog.c \
	src/ui/gui/examine-dialog.h \
	src/ui/gui/executor.c \
	src/ui/gui/executor.h \
	src/ui/gui/find-dialog.c \
	src/ui/gui/find-dialog.h \
	src/ui/gui/factor-dialog.c \
	src/ui/gui/factor-dialog.h \
	src/ui/gui/frequencies-dialog.c \
	src/ui/gui/frequencies-dialog.h \
	src/ui/gui/goto-case-dialog.c \
	src/ui/gui/goto-case-dialog.h \
	src/ui/gui/helper.c \
	src/ui/gui/help-menu.c \
	src/ui/gui/help-menu.h \
	src/ui/gui/helper.h \
	src/ui/gui/k-related-dialog.c \
	src/ui/gui/k-related-dialog.h \
	src/ui/gui/main.c \
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
	src/ui/gui/psppire-encoding-selector.c \
	src/ui/gui/psppire-encoding-selector.h \
	src/ui/gui/psppire-hbuttonbox.h \
	src/ui/gui/psppire-keypad.h \
	src/ui/gui/psppire-output-window.c \
	src/ui/gui/psppire-output-window.h \
	src/ui/gui/psppire-var-view.c \
	src/ui/gui/psppire-var-view.h \
	src/ui/gui/psppire-selector.h \
	src/ui/gui/psppire-select-dest.c \
	src/ui/gui/psppire-select-dest.h \
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
	src/ui/gui/roc-dialog.c \
	src/ui/gui/roc-dialog.h \
	src/ui/gui/select-cases-dialog.c \
	src/ui/gui/select-cases-dialog.h \
	src/ui/gui/sort-cases-dialog.c \
	src/ui/gui/sort-cases-dialog.h \
	src/ui/gui/split-file-dialog.c \
	src/ui/gui/split-file-dialog.h \
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
