## Process this file with automake to produce Makefile.in  -*- makefile -*-


pkgsysconf_DATA = \
	config/devices \
	config/papersize \
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
 
EXTRA_DIST += $(pkgsysconf_DATA)

# A `private installation' in my terms is just having the appropriate
# configuration files in ~/.pspp instead of a global configuration
# location.  So I let those files be installed automatically.

private-install:
	$(mkinstalldirs) $$HOME/.pspp $$HOME/.pspp/psfonts
	cd $(top_srcdir); cp $(pkgsysconf_DATA) $$HOME/.pspp

private-uninstall:
	-cd $$HOME/.pspp && rm $(notdir $(pkgsysconf_DATA))
	-rmdir $$HOME/.pspp/psfonts $$HOME/.pspp

