## Process this file with automake to produce Makefile.in  -*- makefile -*-

AUTOMAKE_OPTIONS = gnits 1.4

SUBDIRS = doc intl po lib src config tests

pkgdocdir = $(prefix)/doc/@PACKAGE@
pkgdoc_DATA = NEWS README TODO
noinst_DATA = AUTHORS THANKS

DISTCLEANFILES = pref.h

dist-hook:
	cp -rp $(top_srcdir)/sysdeps $(distdir)
	cp -rp $(top_srcdir)/examples $(distdir)

# A `private installation' in my terms is just having the appropriate
# configuration files in ~/.pspp instead of a global configuration
# location.  So I let those files be installed automatically.

private-install:
	$(MAKE) private-install -C config
private-uninstall:
	$(MAKE) private-uninstall -C config

EXTRA_DIST = NEWS ONEWS TODO pref.h.orig reconfigure configure

MAINTAINERCLEANFILES = Makefile.in configure aclocal.m4 config.h.in

