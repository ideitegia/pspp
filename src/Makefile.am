## Process this file with automake to produce Makefile.in  -*- makefile -*-

# PSPP

include $(top_srcdir)/src/Make.build

SUBDIRS = expressions

# If you change this, you must also change the corresponding line in 
# config/Makefile.am
pkgsysconfdir = $(sysconfdir)/@PACKAGE@

bin_PROGRAMS = pspp

MAINTAINERCLEANFILES = Makefile.in
EXTRA_DIST = $(q_sources_q) q2c.c
ETAGS_ARGS = -l c $(q_sources_c)
SUFFIXES = .q

$(q_sources_c): q2c$(EXEEXT)
.q.c:
	./q2c $< $@

q_sources_c = correlations.c crosstabs.c examine.c file-handle.c	\
frequencies.c list.c means.c oneway.c set.c  t-test.c 

CLEANFILES=$(q_sources_c)
DISTCLEANFILES=version.c

q_sources_q = correlations.q crosstabs.q examine.q file-handle.q	\
frequencies.q list.q means.q oneway.q set.q  t-test.q

if WITHCHARTS
chart_sources = barchart.c \
	box-whisker.c \
	cartesian.c \
	plot-chart.c \
	plot-hist.c \
	piechart.c 
else
chart_sources = dummy-chart.c
endif

pspp_SOURCES = $(q_sources_c) $(chart_sources) \
aggregate.c algorithm.c algorithm.h	\
alloc.c alloc.h apply-dict.c ascii.c autorecode.c bitvector.h		\
bool.h \
calendar.c calendar.h case.c case.h casefile.c casefile.h chart.c	\
chart.h cmdline.c cmdline.h command.c command.def \
command.h compute.c copyleft.c copyleft.h count.c data-in.c data-in.h	\
data-list.c data-list.h data-out.c date.c debug-print.h descript.c	\
devind.c devind.h dfm-read.c dfm-read.h dfm-write.c dfm-write.h	\
dictionary.c dictionary.h do-if.c do-ifP.h error.c \
error.h factor_stats.c factor_stats.h file-handle.h	\
file-type.c filename.c filename.h flip.c font.h format.c format.def	\
format.h formats.c get.c getline.c getline.h glob.c glob.h		\
groff-font.c group.c group.h group_proc.h \
hash.c hash.h histogram.c histogram.h \
html.c htmlP.h include.c inpt-pgm.c lexer.c lexer.h levene.c levene.h \
linked-list.c linked-list.h log.h loop.c magic.c magic.h main.c main.h	\
matrix-data.c mis-val.c misc.c misc.h modify-vars.c			\
moments.c moments.h numeric.c output.c output.h \
percentiles.c percentiles.h permissions.c \
pfm-read.c pfm-read.h	\
pfm-write.c pfm-write.h \
pool.c pool.h postscript.c print.c recode.c	\
rename-vars.c repeat.c repeat.h sample.c sel-if.c settings.h		\
sfm-read.c sfm-read.h sfm-write.c sfm-write.h sfmP.h som.c som.h	\
sort.c sort.h split-file.c str.c str.h subclist.c subclist.h \
sysfile-info.c tab.c tab.h temporary.c stat.h mkfile.c mkfile.h \
title.c  val.h val-labs.c value-labels.c value-labels.h		\
var-labs.c var.h vars-atr.c vars-prs.c vector.c version.h	\
vfm.c vfm.h vfmP.h weight.c 


pspp_LDADD = \
	../lib/misc/libmisc.a			\
	expressions/libexpressions.a		\
	-lplot \
	@LIBINTL@

nodist_pspp_SOURCES = version.c

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
