## Process this file with automake to produce Makefile.in  -*- makefile -*-

bench:
	make BENCHMARK=1 check

TESTS_ENVIRONMENT=top_srcdir=${top_srcdir}
TESTS = command/aggregate.sh \
	command/autorecod.sh \
	command/beg-data.sh \
	command/bignum.sh \
	command/count.sh \
	command/descriptives.sh \
	command/flip.sh \
	command/lag.sh \
	command/print.sh \
	command/sort.sh \
	command/tabs.sh \
	command/split-file.sh \
	bugs/double-frequency.sh \
	bugs/html-frequency.sh \
	bugs/crosstabs.sh

noinst_PROGRAMS = gengarbage

noinst_DATA = sort.data
sort.data: gengarbage$(EXTEXT)
	./gengarbage | head -1000 > $@

EXTRA_DIST = bignum.data	\
bignum.stat compute.stat count.stat crosstabs.stat data-fmts.stat	\
data-list.data data-list.stat descript.stat do-if.stat do-repeat.stat	\
expr.stat file-lab.stat filter.stat flip.stat gengarbage.c		\
inpt-pgm.stat lag.stat list.data list.stat loop.stat mtch-file.stat	\
means.stat mdfy-vars.stat print.stat pcs-if.stat recode.stat		\
repeating.stat reread.data reread.stat sample.stat sort.stat		\
splt-file.stat sys-info.stat t-test.stat tabs.stat temporary.stat	\
time-date.stat vector.stat weighting.data weighting.stat $(TESTS)

dist-hook:
	cp -rp $(srcdir)/expect $(distdir)

DISTCLEANFILES = *.save sort.data pspp.* foo* msgs *.actual

MAINTAINERCLEANFILES = Makefile.in
