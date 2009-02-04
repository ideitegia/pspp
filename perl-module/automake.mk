## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

module_sources = \
 perl-module/Changes \
 perl-module/COPYING \
 perl-module/Examples.pod \
 perl-module/Makefile.PL \
 perl-module/MANIFEST \
 perl-module/ppport.h \
 perl-module/PSPP.xs \
 perl-module/README \
 perl-module/typemap \
 perl-module/lib/PSPP.pm \
 perl-module/t/Pspp.t

perl-module/pspp-module-config: Makefile
	target=`mktemp`;\
	echo '%Locations = (' > $$target ;\
	printf "  SourceDir => '" >> $$target ;\
	(cd $(top_srcdir) && echo `pwd`\', ) >> $$target ;\
	printf "  BuildDir => '" >> $$target ;\
	(cd $(top_builddir) && echo `pwd`\' ) >> $$target ;\
	echo ');' >> $$target ;\
	cp $$target $(top_builddir)/perl-module/pspp-module-config

perl-module/Makefile: perl-module/Makefile.PL perl-module/pspp-module-config
	cd perl-module && $(PERL) Makefile.PL PREFIX=$(prefix)

perl-module/PSPP-Perl-$(VERSION).tar.gz: $(module_sources)
	$(RM) $@
	cd perl-module && $(MAKE) $(AM_MAKEFLAGS) tardist

PHONY += module-make
module-make: perl-module/Makefile
	cd perl-module && $(MAKE) $(AM_MAKEFLAGS)

all-local: 
	if test x"$(top_builddir)" != x"$(top_srcdir)" ; then \
	 for f in $(module_sources); do \
	  destdir=`dirname $$f` ;\
	  mkdir -p $$destdir ;\
	  if test "$(top_srcdir)/$$f" -nt "$(top_builddir)/$$f" ; then \
		 cp $(top_srcdir)/$$f $$destdir ; \
		 echo cp $(top_srcdir)/$$f $$destdir ; \
	  fi ; \
	 done \
	fi
	$(MAKE) $(AM_MAKEFLAGS) module-make perl-module/PSPP-Perl-$(VERSION).tar.gz

check-local:
	loc=`pwd` ; cd $(top_builddir)/src/.libs ; llp=`pwd` ; cd $$loc ;  \
	LD_LIBRARY_PATH=$$llp sh -c "cd perl-module && $(MAKE) $(AM_MAKEFLAGS) test"


clean-local:
	cd perl-module && $(MAKE) $(AM_MAKEFLAGS) clean
	if test x"$(top_builddir)" != x"$(top_srcdir)" ; then \
	  $(RM) $(module_sources) ; \
	fi
	$(RM) perl-module/Makefile.old

CLEANFILES += \
        perl-module/PSPP-Perl-$(VERSION).tar.gz \
	perl-module/pspp-module-config \
	perl-module/const-c.inc \
	perl-module/const-xs.inc 

EXTRA_DIST +=  $(module_sources)
