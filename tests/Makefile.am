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
	command/filter.sh \
	command/flip.sh \
	command/lag.sh \
	command/list.sh \
	command/loop.sh \
	command/percentiles.sh \
	command/print.sh \
	command/sample.sh \
	command/sort.sh \
	command/tabs.sh \
	command/split-file.sh \
	command/weight.sh \
	bugs/double-frequency.sh \
	bugs/html-frequency.sh \
	bugs/crosstabs.sh

noinst_PROGRAMS = gengarbage

noinst_DATA = sort.data
sort.data: gengarbage$(EXTEXT)
	./gengarbage | head -1000 > $@

EXTRA_DIST = $(TESTS)

DISTCLEANFILES = *.save sort.data pspp.* foo* msgs *.actual

MAINTAINERCLEANFILES = Makefile.in
