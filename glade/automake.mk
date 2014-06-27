## Process this file with automake to produce Makefile.in  -*- makefile -*-

module_LTLIBRARIES = libglade-psppire.la

moduledir = `pkg-config --variable=moduledir gladeui-1.0`
catalogdir = `pkg-config --variable=catalogdir gladeui-1.0`
pixmapdir = `pkg-config --variable=pixmapdir gladeui-1.0`

small_pixmapdir = $(pixmapdir)/16x16
large_pixmapdir = $(pixmapdir)/22x22

libglade_psppire_la_SOURCES = \
	glade/dialog.c \
	glade/bbox.c \
	glade/selector.c \
	glade/acr.c \
	glade/dictview.c \
	glade/var-view.c \
	src/ui/gui/helper.c \
	src/ui/gui/psppire-conf.c \
	src/ui/gui/psppire-acr.c \
	src/ui/gui/psppire-buttonbox.c \
	src/ui/gui/psppire-hbuttonbox.c \
	src/ui/gui/psppire-vbuttonbox.c \
	src/ui/gui/psppire-dialog.c \
	src/ui/gui/psppire-keypad.c \
	src/ui/gui/psppire-dictview.c \
	src/ui/gui/psppire-selector.c \
	src/ui/gui/psppire-select-dest.c \
	src/ui/gui/psppire-var-view.c \
	src/ui/gui/psppire-window-base.c

dist_catalog_DATA = \
	glade/psppire.xml

dist_small_pixmap_DATA = \
	glade/icons/16x16/psppire-acr.png \
	glade/icons/16x16/psppire-hbuttonbox.png \
	glade/icons/16x16/psppire-vbuttonbox.png \
	glade/icons/16x16/psppire-dialog.png \
	glade/icons/16x16/psppire-keypad.png \
	glade/icons/16x16/psppire-selector.png 

dist_large_pixmap_DATA = \
	glade/icons/22x22/psppire-acr.png \
	glade/icons/22x22/psppire-hbuttonbox.png \
	glade/icons/22x22/psppire-vbuttonbox.png \
	glade/icons/22x22/psppire-dialog.png \
	glade/icons/22x22/psppire-keypad.png \
	glade/icons/22x22/psppire-selector.png 


libglade_psppire_la_CFLAGS = $(GLADE_UI_CFLAGS) $(GLADE_CFLAGS) \
	$(GTKSOURCEVIEW_CFLAGS) -I $(top_srcdir)/src/ui/gui -DDEBUGGING

libglade_psppire_la_LIBADD = gl/libgl.la
