## Process this file with automake to produce Makefile.in  -*- makefile -*-


pkgsysconf_DATA = \
	config/devices \
	config/html-prologue \
	config/papersize \
	config/ps-prologue

EXTRA_DIST += $(pkgsysconf_DATA)

# A `private installation' in my terms is just having the appropriate
# configuration files in ~/.pspp instead of a global configuration
# location.  So I let those files be installed automatically.

private-install:
	$(mkinstalldirs) $$HOME/.pspp
	cd $(top_srcdir); cp $(pkgsysconf_DATA) $$HOME/.pspp

private-uninstall:
	-cd $$HOME/.pspp;  $(RM) $(notdir $(pkgsysconf_DATA))
	-rmdir $$HOME/.pspp

