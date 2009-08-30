## Process this file with automake to produce Makefile.in  -*- makefile -*-

AUTOMAKE_OPTIONS = gnits 1.10 subdir-objects

SUBDIRS = gl po
DISTCLEANFILES = ./po/stamp-po xconfigure 
BUILT_SOURCES =

AM_CPPFLAGS = -I$(top_srcdir)/gl -I$(top_builddir)/gl

AM_CFLAGS=

if cc_is_gcc
AM_CFLAGS+=-Wall -W -Wwrite-strings -Wstrict-prototypes \
-Wpointer-arith -Wno-sign-compare -Wmissing-prototypes
endif

CC_FOR_BUILD = @CC_FOR_BUILD@
EXEEXT_FOR_BUILD = @EXEEXT_FOR_BUILD@
.q.c:
	@$(MKDIR_P) `dirname $@`
	./src/language/lexer/q2c$(EXEEXT_FOR_BUILD) $< $@

$(all_q_sources:.q=.c): src/language/lexer/q2c$(EXEEXT_FOR_BUILD)
all_q_sources =

pkgsysconfdir = $(sysconfdir)/@PACKAGE@


EXTRA_DIST = OChangeLog ONEWS config.rpath pspp-mode.el

CLEANFILES = 
CLEAN_LOCAL =
ACLOCAL_AMFLAGS = -I m4 -I gl/m4
noinst_LIBRARIES=
noinst_LTLIBRARIES=
noinst_PROGRAMS=
check_PROGRAMS=
bin_PROGRAMS=
DIST_HOOKS =
INSTALL_DATA_HOOKS = 
UNINSTALL_DATA_HOOKS =
PHONY =

generate-changelog:
	if test -d $(top_srcdir)/.git; then			\
	  $(top_srcdir)/gitlog-to-changelog --since=2008-07-27	\
	    > $(distdir)/cl-t;					\
	  rm -f $(distdir)/ChangeLog;				\
	  mv $(distdir)/cl-t $(distdir)/ChangeLog;		\
	fi

DIST_HOOKS += generate-changelog


include $(top_srcdir)/lib/automake.mk
include $(top_srcdir)/doc/automake.mk
include $(top_srcdir)/config/automake.mk
include $(top_srcdir)/examples/automake.mk
include $(top_srcdir)/src/automake.mk

include $(top_srcdir)/tests/automake.mk

if WITH_GUI_TOOLS
include $(top_srcdir)/glade/automake.mk
endif

if WITH_PERL_MODULE
include $(top_srcdir)/perl-module/automake.mk
endif

PHONY += $(DIST_HOOKS) $(INSTALL_DATA_HOOKS) $(UNINSTALL_DATA_HOOKS)

.PHONY: $(PHONY)

dist-hook: $(DIST_HOOKS)

install-data-hook: $(INSTALL_DATA_HOOKS)

uninstall-hook: $(UNINSTALL_DATA_HOOKS)

clean-local: $(CLEAN_LOCAL)
