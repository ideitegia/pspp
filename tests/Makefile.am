## Process this file with automake to produce Makefile.in  -*- makefile -*-

TESTS_ENVIRONMENT=top_srcdir=${top_srcdir}
TESTS = command/aggregate.sh \
	command/autorecod.sh \
	command/beg-data.sh \
	command/bignum.sh \
	command/count.sh \
	command/compute.sh \
	command/descriptives.sh \
	command/file-label.sh \
	command/flip.sh \
	command/lag.sh \
	command/list.sh \
	command/loop.sh \
	command/print.sh \
	command/sample.sh \
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

EXTRA_DIST = \
crosstabs.stat data-fmts.stat	\
data-list.data do-if.stat do-repeat.stat	\
expr.stat filter.stat gengarbage.c		\
inpt-pgm.stat list.data mtch-file.stat  \
means.stat mdfy-vars.stat pcs-if.stat recode.stat \
repeating.stat reread.data reread.stat  \
sys-info.stat t-test.stat temporary.stat	\
time-date.stat vector.stat weighting.data weighting.stat $(TESTS)

DISTCLEANFILES = *.save sort.data pspp.* foo* msgs *.actual

MAINTAINERCLEANFILES = Makefile.in
