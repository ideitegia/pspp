## Process this file with automake to produce Makefile.in  -*- makefile -*-


examplesdir = $(pkgdatadir)/examples

examples_DATA = \
	examples/descript.sps \
	examples/grid.sps \
	examples/hotel.sav \
	examples/physiology.sav \
	examples/repairs.sav \
	examples/regress.sps \
	examples/regress_categorical.sps

EXTRA_DIST += $(examples_DATA)
