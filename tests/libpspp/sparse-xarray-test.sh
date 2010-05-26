#!/bin/sh

# This program tests the sparse_xarray abstract data type implementation.

set -e

: ${top_builddir:=.}
RUN_TEST="${top_builddir}/tests/libpspp/sparse-xarray-test$EXEEXT --verbosity=0"

# Each on-disk sparse_xarray eats up a file descriptor, so for the
# tests that involve on-disk sparse_xarrays we need to limit the
# maximum length of the queue.  Figure out how many file descriptors
# we can let the test program open at once.
OPEN_MAX=`getconf OPEN_MAX 2>/dev/null`
case $OPEN_MAX in
    [0-9]*)
	# Divide by 2 because some fds are used by other code.
	queue_limit=`expr $OPEN_MAX / 2` 
	;;
    undefined) 
	# Assume that any system with a dynamic fd limit has a large limit.
	queue_limit=500 
	;;
    *)
	case `uname -m 2>/dev/null` in
	    CYGWIN*)
                # Cygwin claims a 256-fd limit as OPEN_MAX in <limits.h>.
		queue_limit=128
		;;
	    MINGW*)
		# The following email claims that Mingw should have a
		# 2048-fd limit:
		# http://www.mail-archive.com/squid-users@squid-cache.org/msg35249.html
		queue_limit=1024
		;;
	    *)
		# This seems fairly conservative these days.
		queue_limit=50
		;;
	esac
	;;
esac

# Test completely in-memory sparse_xarray.  --values=3 would be a
# slightly better test but takes much longer.
$RUN_TEST --columns=3 --max-rows=3 --max-memory-rows=3 --values=2

# Test on-disk sparse_xarrays.
for max_memory_rows in 0 1 2; do
    $RUN_TEST --columns=2 --max-rows=3 --max-memory-rows=$max_memory_rows --values=2 --queue-limit=$queue_limit
done

# Test copying back and forth between a pair of sparse_xarrays in
# memory.
$RUN_TEST --columns=2 --max-rows=2 --max-memory-rows=2 --values=2 --xarrays=2 --no-write-rows --no-copy-columns

# Test copying back and forth between a pair of sparse_xarrays on
# disk.  These parameters are ridiculously low, but it's necessary
# unless we want the tests to take a very long time.
for max_memory_rows in 0 1; do
    $RUN_TEST --columns=1 --max-rows=2 --max-memory-rows=$max_memory_rows --values=2 --xarrays=2 --queue-limit=`expr $queue_limit / 2` --no-write-rows --no-copy-columns
done
