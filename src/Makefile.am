## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

# If you change this, you must also change the corresponding line in 
# config/Makefile.am
pkgsysconfdir = $(sysconfdir)/@PACKAGE@

bin_PROGRAMS = pspp



AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src -I$(top_srcdir)/lib	\
-I$(top_srcdir)/intl 

AM_CFLAGS=

if cc_is_gcc
AM_CFLAGS+=-Wall -W -Wwrite-strings -Wstrict-prototypes \
-Wpointer-arith -Wno-sign-compare -Wmissing-prototypes \
-ansi 
endif

if unix
AM_CFLAGS+=-Dunix
endif

if msdos
AM_CFLAGS+=-D__MSDOS__
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

q_sources_c = correlations.c crosstabs.c file-handle.c	\
frequencies.c list.c means.c set.c  t-test.c

q_sources_q = correlations.q crosstabs.q file-handle.q	\
frequencies.q list.q means.q set.q  t-test.q

pspp_SOURCES = $(q_sources_c) aggregate.c algorithm.c algorithm.h	\
alloc.c alloc.h apply-dict.c ascii.c autorecode.c bitvector.h		\
casefile.c casefile.h cmdline.c cmdline.h command.c command.def		\
command.h compute.c copyleft.c copyleft.h count.c data-in.c data-in.h	\
data-list.c data-list.h data-out.c date.c debug-print.h descript.c	\
devind.c devind.h dfm.c dfm.h dictionary.c do-if.c do-ifP.h error.c	\
error.h expr-evl.c expr-opt.c expr-prs.c expr.h exprP.h expr.def        \
file-handle.h	\
file-type.c filename.c filename.h flip.c font.h format.c format.def	\
format.h formats.c get.c getline.c getline.h glob.c glob.h		\
groff-font.c hash.c hash.h html.c htmlP.h include.c inpt-pgm.c lexer.c	\
lexer.h levene.c levene.h log.h loop.c magic.c magic.h main.c main.h	\
matrix-data.c mis-val.c misc.c misc.h modify-vars.c			\
moments.c moments.h numeric.c output.c output.h pfm-read.c pfm-write.c	\
pfm.h pool.c pool.h postscript.c print.c random.c random.h recode.c	\
rename-vars.c repeat.c repeat.h sample.c sel-if.c settings.h		\
sfm-read.c sfm-write.c sfm.h sfmP.h som.c som.h sort.c sort.h		\
split-file.c str.c str.h sysfile-info.c tab.c tab.h temporary.c		\
stat.h \
title.c t-test.h val.h val-labs.c value-labels.c value-labels.h		\
var-labs.c var.h vars-atr.c vars-prs.c vector.c version.c version.h	\
vfm.c vfm.h vfmP.h weight.c workspace.c workspace.h

pspp_LDADD = ../lib/julcal/libjulcal.a		\
	../lib/misc/libmisc.a			\
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
