## Process this file with automake to produce Makefile.in  -*- makefile -*-


dist_pkgsysconf_DATA = \
	config/devices \
	config/papersize

psfontsdir = $(pkgsysconfdir)/psfonts
dist_psfonts_DATA = \
	config/psfonts/Helvetica-Bold.afm \
	config/psfonts/Times-Bold.afm \
	config/psfonts/Courier-Bold.afm \
	config/psfonts/Helvetica-BoldOblique.afm \
	config/psfonts/Times-BoldItalic.afm \
	config/psfonts/Courier-BoldOblique.afm \
	config/psfonts/Helvetica-Oblique.afm \
	config/psfonts/Times-Italic.afm \
	config/psfonts/Courier-Oblique.afm \
	config/psfonts/Helvetica.afm \
	config/psfonts/Times-Roman.afm \
	config/psfonts/Courier.afm
 
# A `private installation' in my terms is just having the appropriate
# configuration files in ~/.pspp instead of a global configuration
# location.  So I let those files be installed automatically.

private-install:
	$(mkinstalldirs) $$HOME/.pspp
	cd $(top_srcdir) && cp $(dist_pkgsysconf_DATA) $$HOME/.pspp
	$(mkinstalldirs) $$HOME/.pspp/psfonts
	cd $(top_srcdir) && cp $(dist_psfonts_DATA) $$HOME/.pspp/psfonts


private-uninstall:
	-cd $$HOME/.pspp && rm $(notdir $(dist_pkgsysconf_DATA))
	-cd $$HOME/.pspp/psfonts && rm $(notdir $(dist_psfonts_DATA))
	-rmdir $$HOME/.pspp/psfonts $$HOME/.pspp

