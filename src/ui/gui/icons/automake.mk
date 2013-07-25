include $(top_srcdir)/src/ui/gui/icons/manifest

EXTRA_DIST += $(icons) $(icon_srcs)

themedir = $(DESTDIR)$(datadir)/icons/hicolor

sizes=16x16 22x22  24x24 32x32 48x48 256x256

install-mimetypes:
	for size in $(sizes); do \
		$(MKDIR_P) $(themedir)/$$size/mimetypes ; \
		if (cd $(top_srcdir)/src/ui/gui/icons/mimetypes/$$size && \
			(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
			$(INSTALL_DATA) $(top_srcdir)/src/ui/gui/icons/mimetypes/$$size/* $(themedir)/$$size/mimetypes ; \
		fi ; \
	done


uninstall-mimetypes:
	for size in $(sizes); do \
		if (cd $(top_srcdir)/src/ui/gui/icons/mimetypes/$$size && \
			(test ! "`printf '%s %s %s' . .. *`" = '. .. *' || test -f '*')) 2> /dev/null ; then \
			  rm -rf $(themedir)/$$size/mimetypes/application-x-spss-* ; \
		fi ; \
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



INSTALL_DATA_HOOKS += install-icons install-mimetypes
UNINSTALL_DATA_HOOKS += uninstall-icons uninstall-mimetypes
