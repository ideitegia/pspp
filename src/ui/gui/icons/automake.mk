include $(top_srcdir)/src/ui/gui/icons/manifest

EXTRA_DIST += $(icons) $(icon_srcs)

themedir = $(DESTDIR)$(datadir)/icons/hicolor

sizes=16x16 22x22  24x24 32x32 48x48 256x256

install-ext-icons:
	for context in apps mimetypes; do \
		for size in $(sizes); do \
		$(MKDIR_P) $(themedir)/$$size/$$context ; \
			if (cd $(top_srcdir)/src/ui/gui/icons/$$context/$$size && \
				(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
				$(INSTALL_DATA) $(top_srcdir)/src/ui/gui/icons/$$context/$$size/* $(themedir)/$$size/$$context ; \
			fi ; \
		done ; \
	done


uninstall-ext-icons:
	for context in apps mimetypes; do \
		for size in $(sizes); do \
			if (cd $(top_srcdir)/src/ui/gui/icons/$$context/$$size && \
				(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
				  rm -rf $(themedir)/$$size/$$context/application-x-spss-* ; \
				  rm -rf $(themedir)/$$size/$$context/pspp* ; \
			fi ; \
		done ; \
	done


install-icons:
	for context in actions categories ; do \
	  $(MKDIR_P) $(DESTDIR)$(pkgdatadir)/$$context; \
	  for size in $(sizes); do \
		if (cd $(top_srcdir)/src/ui/gui/icons/$$context/$$size && \
			(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
			$(MKDIR_P) $(DESTDIR)$(pkgdatadir)/$$context/$$size ; \
			$(INSTALL_DATA) $(top_srcdir)/src/ui/gui/icons/$$context/$$size/* $(DESTDIR)$(pkgdatadir)/$$context/$$size ; \
		fi ; \
	  done ; \
	done



uninstall-icons:
	for context in actions categories ; do \
	  for size in $(sizes); do \
		if (cd $(top_srcdir)/src/ui/gui/icons/$$context/$$size && \
			(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
			rm -rf $(DESTDIR)$(pkgdatadir)/$$context/$$size ; \
		fi ; \
	  done ; \
	done


INSTALL_DATA_HOOKS += install-icons install-ext-icons
UNINSTALL_DATA_HOOKS += uninstall-icons uninstall-ext-icons

if HAVE_GUI
dist_src_ui_gui_psppire_DATA += \
	$(top_srcdir)/src/ui/gui/icons/splash.png \
	$(top_srcdir)/src/ui/gui/icons/about-logo.png

src_ui_gui_psppire_SOURCES += \
	src/ui/gui/icons/icon-names.c \
	src/ui/gui/icons/icon-names.h

endif

