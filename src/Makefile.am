## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

# If you change this, you must also change the corresponding line in 
# config/Makefile.am
pkgsysconfdir = $(sysconfdir)/@PACKAGE@

bin_PROGRAMS = pspp

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src -I$(top_srcdir)/lib	\
-I$(top_srcdir)/intl

if cc_is_gcc
AM_CFLAGS=-Wall -W -Wno-uninitialized -Wwrite-strings -Wstrict-prototypes \
-Wpointer-arith -Wno-sign-compare -Wmissing-prototypes 
endif

CLEANFILES =  $(q_sources_c) version.c
DISTCLEANFILES = foo 
MAINTAINERCLEANFILES = Makefile.in
EXTRA_DIST = $(q_sources_q) q2c.c
ETAGS_ARGS = -l c $(q_sources_c)
SUFFIXES = .q

$(q_sources_c): q2c$(EXEEXT)
.q.c:
	./q2c $< $@

q_sources_c = correlations.c crosstabs.c descript.c file-handle.c	\
frequencies.c list.c means.c set.c t-test.c

q_sources_q = correlations.q crosstabs.q descript.q file-handle.q	\
frequencies.q list.q means.q set.q t-test.q

pspp_SOURCES = $(q_sources_c) \
aggregate.c algorithm.c algorithm.h alloc.c alloc.h	\
apply-dict.c approx.h ascii.c autorecode.c bitvector.h	\
cmdline.c cmdline.h command.c command.def command.h compute.c		\
count.c data-in.c data-in.h data-list.c	\
data-out.c debug-print.h devind.c devind.h dfm.c dfm.h	\
dictionary.c do-if.c do-ifP.h error.c error.h expr-evl.c expr-opt.c	\
expr-prs.c expr.h exprP.h file-handle.h file-type.c	\
filename.c filename.h flip.c font.h format.c format.def format.h	\
formats.c get.c getline.c getline.h glob.c glob.h		\
groff-font.c hash.c hash.h heap.c heap.h html.c htmlP.h include.c	\
inpt-pgm.c lexer.c lexer.h levene.c levene.h \
log.h loop.c magic.c magic.h main.c	\
main.h matrix-data.c matrix.c matrix.h mis-val.c misc.c misc.h	\
modify-vars.c numeric.c output.c output.h pfm-read.c pfm-write.c pfm.h	\
pool.c pool.h postscript.c print.c random.c random.h recode.c		\
rename-vars.c repeat.c repeat.h sample.c sel-if.c  settings.h	\
sfm-read.c sfm-write.c sfm.h sfmP.h som.c som.h sort.c sort.h		\
split-file.c stat.h stats.c stats.h str.c str.h sysfile-info.c tab.c	\
tab.h temporary.c title.c t-test.h val-labs.c value-labels.c		\
value-labels.h var-labs.c var.h vars-atr.c vars-prs.c vector.c		\
version.c version.h vfm.c vfm.h vfmP.h weight.c 

pspp_LDADD =	../lib/julcal/libjulcal.a		\
	../lib/misc/libmisc.a			\
	../lib/dcdflib/libdcdflib.a		\
	@LIBINTL@

version.c:
	echo "#include <config.h>" > version.c
	echo "#include \"version.h\"" > version.c
	echo "const char bare_version[] = \"@VERSION@\";" >> version.c
	echo "const char version[] = \"GNU @PACKAGE@ @VERSION@\";" >> version.c
	echo "const char stat_version[] = \"GNU @PACKAGE@ @VERSION@ \
(`date`).\";" >> version.c
	echo "const char host_system[] = \"$(host_triplet)\";" >> version.c
	echo "const char build_system[] = \"$(build_triplet)\";" >> version.c
	echo "const char default_config_path[] =\
\"~/.pspp:$(pkgsysconfdir)\";" >> version.c
	echo "const char include_path[] =\
\"./:~/.pspp/include:$(pkgdatadir)\";" >> version.c
	echo "const char groff_font_path[] = \"~/.pspp/font:\" \\" >> version.c
	echo "	\"$(pkgdatadir)/font:\" \\" >> version.c
	echo "	\"/usr/local/lib/groff/font:\" \\" >> version.c
	echo "	\"/usr/lib/groff/font:\" \\" >> version.c
	echo "	\"/usr/local/share/groff/font:\" \\" >> version.c
	echo "	\"/usr/share/groff/font\";" >> version.c
	echo "const char locale_dir[] = \"$(datadir)/locale\";" >> version.c


noinst_PROGRAMS = q2c
q2c_SOURCES = q2c.c
