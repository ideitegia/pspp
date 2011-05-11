bin_PROGRAMS += utilities/pspp-dump-sav
utilities_pspp_dump_sav_SOURCES = \
	src/libpspp/integer-format.c \
	src/libpspp/float-format.c \
	utilities/pspp-dump-sav.c
utilities_pspp_dump_sav_CPPFLAGS = $(AM_CPPFLAGS) -DINSTALLDIR=\"$(bindir)\"

