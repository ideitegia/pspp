## Process this file with automake to produce Makefile.in  -*- makefile -*-


# If you change this, you must also change the corresponding line in
# src/Makefile.am
pkgsysconfdir = $(sysconfdir)/@PACKAGE@

pkgsysconf_DATA = devices html-prologue papersize ps-prologue
EXTRA_DIST = devices html-prologue papersize ps-prologue

# A `private installation' in my terms is just having the appropriate
# configuration files in ~/.pspp instead of a global configuration
# location.  So I let those files be installed automatically.

private-install:
	$(mkinstalldirs) $$HOME/.pspp
	cd $(srcdir); cp $(pkgsysconf_DATA) $$HOME/.pspp
private-uninstall:
	-cd $$HOME/.pspp; rm -f $(pkgsysconf_DATA)
	-rmdir $$HOME/.pspp

MAINTAINERCLEANFILES = Makefile.in
