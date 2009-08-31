include $(top_srcdir)/po/Makevars

XGETTEXT=xgettext
MSGMERGE=msgmerge
MSGFMT=msgfmt

POFILES=po/en_GB.po po/nl.po

POTFILE=po/$(DOMAIN).pot

$(POTFILE): $(DIST_SOURCES)
	@$(MKDIR_P) po
	$(XGETTEXT) --directory=$(top_srcdir) $(DIST_SOURCES) \
	$(XGETTEXT_OPTIONS) \
	--copyright-holder="$(COPYRIGHT_HOLDER)" \
	--package-name=$(PACKAGE) \
	--package-version=$(VERSION) \
	--msgid-bugs-address=$(MSGID_BUGS_ADDRESS) \
	--add-comments='TRANSLATORS:' \
	-o $(POTFILE)


$(POFILES): $(POTFILE)
	$(MSGMERGE) $(top_srcdir)/$* $< -o $@

.po.gmo:
	@$(MKDIR_P) `dirname $@`
	$(MSGFMT) $< -o $@


GMOFILES = $(POFILES:.po=.gmo)

all-hook: $(GMOFILES)

install-data-hook: $(GMOFILES)
	for f in $(GMOFILES); do \
	  lang=`echo $$f | sed -e 's%po/\(.*\)\.gmo%\1%' ` ; \
	  $(INSTALL) -D $$f $(DESTDIR)$(prefix)/share/locale/$$lang/LC_MESSAGES/$(DOMAIN).mo ; \
	done
	

uninstall-hook:
	for f in $(GMOFILES); do \
	  lang=`echo $$f | sed -e 's%po/\(.*\)\.gmo%\1%' ` ; \
	  $(RM) $(DESTDIR)$(prefix)/share/locale/$$lang/LC_MESSAGES/$(DOMAIN).mo ; \
	done


EXTRA_DIST += $(POFILES) $(POTFILE)

CLEANFILES += $(POFILES) $(GMOFILES) $(POTFILE)

