bin_PROGRAMS += utilities/pspp-dump-sav
dist_man_MANS += utilities/pspp-dump-sav.1
utilities_pspp_dump_sav_SOURCES = \
	src/libpspp/integer-format.c \
	src/libpspp/float-format.c \
	utilities/pspp-dump-sav.c
utilities_pspp_dump_sav_CPPFLAGS = $(AM_CPPFLAGS) -DINSTALLDIR=\"$(bindir)\"

bin_PROGRAMS += utilities/pspp-convert
dist_man_MANS += utilities/pspp-convert.1
utilities_pspp_convert_SOURCES = utilities/pspp-convert.c
utilities_pspp_convert_CPPFLAGS = $(AM_CPPFLAGS) -DINSTALLDIR=\"$(bindir)\"
utilities_pspp_convert_LDADD = src/libpspp-core.la

utilities_pspp_convert_LDFLAGS = $(PSPP_LDFLAGS) $(PG_LDFLAGS)
if RELOCATABLE_VIA_LD
utilities_pspp_convert_LDFLAGS += `$(RELOCATABLE_LDFLAGS) $(bindir)`
endif
