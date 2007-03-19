## Process this file with automake to produce Makefile.in  -*- makefile -*-

info_TEXINFOS = doc/pspp.texinfo 

doc_pspp_TEXINFOS = doc/version.texi \
	doc/bugs.texi \
	doc/command-index.texi \
	doc/concept-index.texi \
	doc/configuring.texi \
	doc/data-file-format.texi \
	doc/data-io.texi \
	doc/data-selection.texi \
	doc/expressions.texi \
	doc/files.texi \
	doc/flow-control.texi \
	doc/function-index.texi \
	doc/installing.texi \
	doc/introduction.texi \
	doc/invoking.texi \
	doc/language.texi \
	doc/license.texi \
	doc/ni.texi \
	doc/not-implemented.texi \
	doc/portable-file-format.texi \
	doc/q2c.texi \
	doc/statistics.texi \
	doc/transformation.texi \
	doc/regression.texi \
	doc/utilities.texi \
	doc/variables.texi \
	doc/fdl.texi 

EXTRA_DIST += doc/pspp.man \
	doc/get-commands.pl \
	$(doc_pspp_TEXINFOS)

doc/ni.texi: $(top_srcdir)/src/language/command.def doc/get-commands.pl
	@$(MKDIR_P)  doc
	@PERL@ $(top_srcdir)/doc/get-commands.pl $(top_srcdir)/src/language/command.def > $@
