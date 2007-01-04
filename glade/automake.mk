## Process this file with automake to produce Makefile.in  -*- makefile -*-

module_LTLIBRARIES = libglade-psppire.la

moduledir = `pkg-config --variable=moduledir libgladeui-1.0`
catalogdir = `pkg-config --variable=catalogdir libgladeui-1.0`

libglade_psppire_la_SOURCES = \
	glade/dialog.c \
	glade/bbox.c \
	src/ui/gui/psppire-dialog.c \
	src/ui/gui/psppire-buttonbox.c

nodist_catalog_DATA = \
	glade/psppire.xml


libglade_psppire_la_CFLAGS = $(GLADE_UI_CFLAGS) $(GLADE_CFLAGS) \
	-I src/ui/gui
