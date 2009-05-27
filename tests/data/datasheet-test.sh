#!/bin/sh

# This program tests the datasheet implementation.

set -e

: ${top_builddir:=.}
RUN_TEST="${top_builddir}/tests/data/datasheet-test --verbosity=0"

$RUN_TEST --max-rows=3 --max-columns=3 --backing-rows=0 --backing-columns=0
$RUN_TEST --max-rows=3 --max-columns=3 --backing-rows=3 --backing-columns=3
$RUN_TEST --max-rows=3 --max-columns=3 --backing-rows=3 --backing-columns=1
$RUN_TEST --max-rows=3 --max-columns=3 --backing-rows=1 --backing-columns=3
