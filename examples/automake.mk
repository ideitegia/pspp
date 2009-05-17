## Process this file with automake to produce Makefile.in  -*- makefile -*-


examplesdir = $(pkgdatadir)/examples

examples_DATA = \
	examples/descript.stat \
	examples/hotel.sav \
	examples/physiology.sav \
	examples/repairs.sav \
	examples/regress.stat \
	examples/regress_categorical.stat

EXTRA_DIST += examples/OChangeLog $(examples_DATA)
