## Process this file with automake to produce Makefile.in  -*- makefile -*-

TESTS_ENVIRONMENT=top_srcdir=${top_srcdir}
TESTS = \
	command/aggregate.sh \
	command/autorecod.sh \
	command/beg-data.sh \
	command/bignum.sh \
	command/count.sh \
	command/data-list.sh \
	command/erase.sh \
	command/examine.sh \
	command/examine-extremes.sh \
	command/examine-percentiles.sh \
	command/file-label.sh \
	command/filter.sh \
	command/flip.sh \
	command/import-export.sh \
	command/lag.sh \
	command/list.sh \
	command/loop.sh \
	command/oneway.sh \
	command/oneway-missing.sh \
	command/oneway-with-splits.sh \
	command/permissions.sh \
	command/print.sh \
	command/sample.sh \
	command/sort.sh \
	command/split-file.sh \
	command/t-test-1-indep-val.sh \
	command/t-test-1-sample-missing-anal.sh \
	command/t-test-1-sample-missing-list.sh \
	command/t-test-1s.sh \
	command/t-test-groups.sh \
	command/t-test-indep-missing-anal.sh \
	command/t-test-indep-missing-list.sh \
	command/t-test-paired-missing-anal.sh \
	command/t-test-paired-missing-list.sh \
	command/t-test-pairs.sh \
	command/trimmed-mean.sh \
	command/tabs.sh \
	command/use.sh \
	command/weight.sh \
	bugs/alpha-freq.sh \
	bugs/big-input.sh \
	bugs/comment-at-eof.sh \
	bugs/compute-fmt.sh \
	bugs/crosstabs.sh \
	bugs/data-crash.sh \
	bugs/double-frequency.sh \
	bugs/get.sh \
	bugs/examine-1sample.sh \
	bugs/html-frequency.sh \
	bugs/multipass.sh \
	bugs/random.sh \
	bugs/t-test-with-temp.sh \
	bugs/t-test.sh \
	bugs/t-test-alpha.sh \
	bugs/t-test-alpha2.sh \
	bugs/temporary.sh \
	bugs/val-labs.sh \
	bugs/val-labs-trailing-slash.sh \
	bugs/recode-copy-bug.sh \
	bugs/computebug.sh \
	xforms/casefile.sh \
	xforms/expressions.sh \
	stats/descript-basic.sh \
	stats/descript-missing.sh \
	stats/descript-mean-bug.sh \
	stats/moments.sh \
	stats/percentiles-compatible.sh \
	stats/ntiles.sh \
	stats/percentiles-enhanced.sh

noinst_PROGRAMS = gengarbage

noinst_DATA = sort.data
sort.data: gengarbage$(EXTEXT)
	./gengarbage | head -1000 > $@

EXTRA_DIST = $(TESTS) weighting.data data-list.data list.data \
	bugs/computebug.stat bugs/computebug.out \
	bugs/recode-copy-bug-1.stat bugs/recode-copy-bug-2.stat \
	bugs/recode-copy-bug-1.out bugs/recode-copy-bug-2.out

DISTCLEANFILES = *.save sort.data pspp.* foo* msgs *.actual

MAINTAINERCLEANFILES = Makefile.in
