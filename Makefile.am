## Process this file with automake to produce Makefile.in  -*- makefile -*-

AUTOMAKE_OPTIONS = gnits 1.7

SUBDIRS = m4 intl doc lib gl src po config tests examples

DISTCLEANFILES = pref.h

# A `private installation' in my terms is just having the appropriate
# configuration files in ~/.pspp instead of a global configuration
# location.  So I let those files be installed automatically.

private-install:
	$(MAKE) private-install -C config
private-uninstall:
	$(MAKE) private-uninstall -C config

EXTRA_DIST = AUTHORS NEWS ONEWS README TODO THANKS config.rpath	\
mkinstalldirs config.rpath pref.h.orig configure pspp-mode.el

MAINTAINERCLEANFILES = Makefile.in aclocal.m4 

ACLOCAL_AMFLAGS = -I m4 -I gl/m4
