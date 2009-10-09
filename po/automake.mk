include $(top_srcdir)/po/Makevars

XGETTEXT=xgettext
MSGMERGE=msgmerge
MSGFMT=msgfmt

POFILES=po/en_GB.po po/nl.po po/pt_BR.po

POTFILE=po/$(DOMAIN).pot

TRANSLATABLE_FILES = $(DIST_SOURCES) $(all_q_sources)

XGETTEXT_OPTIONS = \
	--copyright-holder="$(COPYRIGHT_HOLDER)" \
	--package-name=$(PACKAGE) \
	--package-version=$(VERSION) \
	--msgid-bugs-address=$(MSGID_BUGS_ADDRESS) \
	--add-comments='TRANSLATORS:'

$(POTFILE): $(TRANSLATABLE_FILES) $(UI_FILES)
	@$(MKDIR_P) po
	$(XGETTEXT) --directory=$(top_srcdir) $(XGETTEXT_OPTIONS)    $(TRANSLATABLE_FILES) --language=C --keyword=_ --keyword=N_ -o $@
	$(XGETTEXT) --directory=$(top_srcdir) $(XGETTEXT_OPTIONS) -j $(UI_FILES) --language=glade -o $@


$(POFILES): $(POTFILE)
	$(MSGMERGE) $(top_srcdir)/$@ $< -o $@


SUFFIXES += .po .gmo
.po.gmo:
	@$(MKDIR_P) `dirname $@`
	$(MSGFMT) $< -o $@


GMOFILES = $(POFILES:.po=.gmo)

ALL_LOCAL += $(GMOFILES)

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

