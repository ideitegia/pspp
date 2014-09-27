## Process this file with automake to produce Makefile.in  -*- makefile -*-

info_TEXINFOS = doc/pspp.texi doc/pspp-dev.texi

doc_pspp_TEXINFOS = doc/version.texi \
	doc/bugs.texi \
	doc/command-index.texi \
	doc/concept-index.texi \
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
	doc/pspp-convert.texi \
	doc/ni.texi \
	doc/not-implemented.texi \
	doc/statistics.texi \
	doc/transformation.texi \
	doc/tutorial.texi \
	doc/tut.texi \
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

$(srcdir)/doc/ni.texi: $(top_srcdir)/src/language/command.def doc/get-commands.pl
	@$(MKDIR_P)  doc
	$(PERL) $(top_srcdir)/doc/get-commands.pl $(top_srcdir)/src/language/command.def > $@

$(srcdir)/doc/tut.texi:
	@$(MKDIR_P) doc
	echo "@set example-dir $(examplesdir)" > $@


# The SED and AWK filters in this rule, are to work-around some nasty bugs in makeinfo version 4.13, which produces
# broken docbook xml.  These workarounds are rather horrible and must be removed asap.
$(srcdir)/doc/pspp.xml: doc/pspp.texi $(doc_pspp_TEXINFOS)
	@$(MKDIR_P)  doc
	$(MAKEINFO) $(AM_MAKEINFOFLAGS) --docbook -I $(top_srcdir) \
		$(top_srcdir)/doc/pspp.texi -o - \
		| $(SED) -e 's/Time-&-Date/Time-\&amp;-Date/g' \
		-e 's/&ldquo;/\&#8220;/g' \
		-e 's/&rdquo;/\&#8221;/g' \
		-e 's/&lsquo;/\&#8216;/g' \
		-e 's/&rsquo;/\&#8217;/g' \
		-e 's/&mdash;/\&#8212;/g' \
		-e 's/&ndash;/\&#8242;/g' \
                -e 's/&eacute;/\&#0233;/g' \
		-e 's/&copy;/\&#0169;/g' \
		-e 's/&minus;/\&#8722;/g' \
		-e 's/&hellip;/\&#8230;/g' \
		-e 's/&bull;/\&#2022;/g' \
		-e 's/&period;/./g' \
		-e 's%\(<figure [^>]*\)>%\1/>%g' \
	 | $(AWK) '/<para>.*<table.*>.*<\/para>/{x=sub("</para>",""); print; s=1;next}/<\/table>/{print; if (s==1) print "</para>"; s=0; next}1' \
	> $@
	$(XMLLINT) --output /dev/null $@ || ( $(RM) $@ ; false)

docbookdir = $(docdir)
dist_docbook_DATA = doc/pspp.xml
 

CLEANFILES += pspp-dev.dvi $(docbook_DATA)

doc: $(INFO_DEPS) $(DVIS) $(PDFS) $(PSS) $(HTMLS) $(dist_docbook_DATA)
.PHONY: doc
