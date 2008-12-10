## Process this file with automake to produce Makefile.in  -*- makefile -*-

info_TEXINFOS = doc/pspp.texinfo doc/pspp-dev.texinfo

doc_pspp_TEXINFOS = doc/version.texi \
	doc/bugs.texi \
	doc/command-index.texi \
	doc/concept-index.texi \
	doc/configuring.texi \
	doc/data-io.texi \
	doc/data-selection.texi \
	doc/expressions.texi \
	doc/files.texi \
	doc/combining.texi \
	doc/flow-control.texi \
	doc/function-index.texi \
	doc/installing.texi \
	doc/introduction.texi \
	doc/invoking.texi \
	doc/language.texi \
	doc/license.texi \
	doc/ni.texi \
	doc/not-implemented.texi \
	doc/statistics.texi \
	doc/transformation.texi \
	doc/regression.texi \
	doc/utilities.texi \
	doc/variables.texi \
	doc/fdl.texi 

doc_pspp_dev_TEXINFOS = doc/version-dev.texi \
	doc/dev/intro.texi \
	doc/dev/concepts.texi \
	doc/dev/syntax.texi \
	doc/dev/data.texi \
	doc/dev/i18n.texi \
	doc/dev/output.texi \
	doc/dev/system-file-format.texi \
	doc/dev/portable-file-format.texi \
	doc/dev/q2c.texi

EXTRA_DIST += doc/pspp.man \
	doc/get-commands.pl

doc/ni.texi: $(top_srcdir)/src/language/command.def doc/get-commands.pl
	@$(MKDIR_P)  doc
	@PERL@ $(top_srcdir)/doc/get-commands.pl $(top_srcdir)/src/language/command.def > $@

# It seems that recent versions of yelp, upon which the gui relies to display the reference
# manual, are broken.  It only works on compressed info files.  So we must compress them.
if WITHGUI
YELP_CHECK = yelp-check
else
YELP_CHECK =
endif
install-data-hook:: $(YELP_CHECK)
	for ifile in $(DESTDIR)$(infodir)/pspp.info-[0-9] \
		$(DESTDIR)$(infodir)/pspp.info  ; do \
	  gzip -f $$ifile ; \
	done

uninstall-hook::
	rm -f $(DESTDIR)$(infodir)/pspp.info-[0-9].gz
	rm -f $(DESTDIR)$(infodir)/pspp.info.gz

EXTRA_DIST += doc/OChangeLog
CLEANFILES += pspp-dev.dvi
