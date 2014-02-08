## Process this file with automake to produce Makefile.in  -*- makefile -*-

noinst_LIBRARIES += lib/gtk-contrib/libxpaned.a

lib_gtk_contrib_libxpaned_a_CFLAGS = $(GTK_CFLAGS) -Wall -DGDK_MULTIHEAD_SAFE=1

lib_gtk_contrib_libxpaned_a_SOURCES = \
	lib/gtk-contrib/gtkxpaned.c \
	lib/gtk-contrib/gtkxpaned.h

EXTRA_DIST += \
	lib/gtk-contrib/README \
	lib/gtk-contrib/COPYING.LESSER

