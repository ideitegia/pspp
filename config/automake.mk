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
