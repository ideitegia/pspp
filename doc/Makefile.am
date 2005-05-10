## Process this file with automake to produce Makefile.in  -*- makefile -*-

info_TEXINFOS = pspp.texinfo 

pspp_TEXINFOS = version.texi \
	bugs.texi \
	command-index.texi \
	concept-index.texi \
	configuring.texi \
	data-file-format.texi \
	data-io.texi \
	data-selection.texi \
	expressions.texi \
	files.texi \
	flow-control.texi \
	function-index.texi \
	installing.texi \
	introduction.texi \
	invoking.texi \
	language.texi \
	license.texi \
	not-implemented.texi \
	portable-file-format.texi \
	q2c.texi \
	statistics.texi \
	transformation.texi \
	utilities.texi \
	variables.texi \
	fdl.texi 

nodist_pspp_TEXINFOS= ni.texi


EXTRA_DIST = pspp.man \
	get-commands.pl \
	$(pspp_TEXINFOS)

CLEANFILES = pspp.info pspp.info-* ni.texi
MAINTAINERCLEANFILES=Makefile.in 

ni.texi: @top_srcdir@/src/command.def @srcdir@/get-commands.pl
	@PERL@ @srcdir@/get-commands.pl $< > $@


$(INFO_DEPS): ni.texi


