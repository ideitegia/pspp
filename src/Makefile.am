## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

# If you change this, you must also change the corresponding line in 
# config/Makefile.am
pkgsysconfdir = $(sysconfdir)/@PACKAGE@

bin_PROGRAMS = pspp

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src -I$(top_srcdir)/lib	\
-I$(top_srcdir)/intl

DISTCLEANFILES = foo $(q_sources_c)
MAINTAINERCLEANFILES = Makefile.in
EXTRA_DIST = $(q_sources_q) q2c.c
ETAGS_ARGS = -l c $(q_sources_c)
SUFFIXES = .q

$(q_sources_c): q2c
.q.c:
	./q2c $< $@

q_sources_c = correlations.c crosstabs.c descript.c file-handle.c	\
frequencies.c list.c means.c set.c t-test.c

q_sources_q = correlations.q crosstabs.q descript.q file-handle.q	\
frequencies.q list.q means.q set.q t-test.q

pspp_SOURCES = aggregate.c alloc.c alloc.h apply-dict.c approx.h	\
ascii.c autorecode.c avl.c avl.h bitvector.h cases.c cases.h cmdline.c	\
command.c command.def command.h compute.c correlations.c count.c	\
crosstabs.c data-in.c data-in.h data-list.c data-out.c debug-print.h	\
descript.c dfm.c dfm.h do-if.c do-ifP.h error.c error.h expr-evl.c	\
expr-opt.c expr-prs.c expr.h exprP.h file-handle.c file-handle.h	\
file-type.c filename.c filename.h flip.c font.h format.c format.def	\
format.h formats.c frequencies.c frequencies.g get.c getline.c		\
getline.h glob.c groff-font.c hash.c hash.h heap.c heap.h html.c	\
htmlP.h include.c inpt-pgm.c inpt-pgm.h lexer.c lexer.h list.c log.h	\
loop.c magic.c magic.h main.c main.h matrix-data.c matrix.c matrix.h	\
means.c mis-val.c misc.c misc.h modify-vars.c numeric.c output.c	\
output.h pfm-read.c pfm-write.c pfm.h pool.c pool.h postscript.c	\
print.c quicksort.c quicksort.h random.c random.h recode.c		\
rename-vars.c repeat.c sample.c sel-if.c set.c settings.h sfm-read.c	\
sfm-write.c sfm.h sfmP.h som.c som.h sort.c sort.h split-file.c stat.h	\
stats.c stats.h str.c str.h sysfile-info.c tab.c tab.h temporary.c	\
title.c t-test.c val-labs.c var-labs.c var.h vars-atr.c vars-prs.c	\
vector.c vector.h version.c version.h vfm.c vfm.h vfmP.h weight.c

LDADD =	../lib/julcal/libjulcal.a		\
	../lib/misc/libmisc.a			\
	../lib/dcdflib/libdcdflib.a		\
	@LIBINTL@

version.c:
	echo "#include <config.h>" > version.c
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
