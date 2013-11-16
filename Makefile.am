## Process this file with automake to produce Makefile.in  -*- makefile -*-

AUTOMAKE_OPTIONS = std-options check-news 1.10.1 subdir-objects

SUBDIRS = gl po
DISTCLEANFILES = ./po/stamp-po xconfigure 
BUILT_SOURCES =

AM_CPPFLAGS = \
	-I$(top_srcdir)/gl \
	-I$(top_builddir)/gl \
	-DINSTALLDIR=\"$(bindir)\"

AM_CFLAGS=

if cc_is_gcc
AM_CFLAGS+=-Wall -W -Wwrite-strings -Wstrict-prototypes \
-Wpointer-arith -Wno-sign-compare -Wmissing-prototypes
endif

.q.c:
	@$(MKDIR_P) `dirname $@`
	./src/language/lexer/q2c$(EXEEXT_FOR_BUILD) $< $@

$(all_q_sources:.q=.c): src/language/lexer/q2c$(EXEEXT_FOR_BUILD)
all_q_sources =

pkgsysconfdir = $(sysconfdir)/$(PACKAGE)


EXTRA_DIST = OChangeLog ONEWS pspp-mode.el

CLEANFILES = 
CLEAN_LOCAL =
ALL_LOCAL =
CHECK_LOCAL =
ACLOCAL_AMFLAGS = -I m4 -I gl/m4
noinst_LIBRARIES=
noinst_LTLIBRARIES=
noinst_PROGRAMS=
check_PROGRAMS=
bin_PROGRAMS=
dist_man_MANS =
DIST_HOOKS =
INSTALL_DATA_HOOKS = 
UNINSTALL_DATA_HOOKS =
PHONY =
SUFFIXES = .q
LDADD = gl/libgl.la

generate-changelog:
	if test -d $(top_srcdir)/.git; then			\
	  $(top_srcdir)/build-aux/gitlog-to-changelog --since=2008-07-27	\
	    > $(distdir)/cl-t;					\
	  rm -f $(distdir)/ChangeLog;				\
	  mv $(distdir)/cl-t $(distdir)/ChangeLog;		\
	fi

DIST_HOOKS += generate-changelog


include $(top_srcdir)/build-aux/automake.mk
include $(top_srcdir)/po/automake.mk
include $(top_srcdir)/lib/automake.mk
include $(top_srcdir)/doc/automake.mk
include $(top_srcdir)/examples/automake.mk
include $(top_srcdir)/src/automake.mk
include $(top_srcdir)/utilities/automake.mk

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
all-local: $(ALL_LOCAL)
check-local: $(CHECK_LOCAL)

# A convenience target to build all the binaries
programs: $(PROGRAMS)
