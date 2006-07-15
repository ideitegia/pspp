## Process this file with automake to produce Makefile.in  -*- makefile -*-


noinst_LIBRARIES += src/language/lexer/liblexer.a

src_language_lexer_liblexer_a_SOURCES = \
	src/language/lexer/lexer.c  src/language/lexer/lexer.h \
	src/language/lexer/subcommand-list.c  \
	src/language/lexer/subcommand-list.h \
	src/language/lexer/format-parser.c \
	src/language/lexer/range-parser.c \
	src/language/lexer/range-parser.h \
	src/language/lexer/variable-parser.c \
	src/language/lexer/variable-parser.h

EXTRA_DIST += src/language/lexer/q2c.c


src/language/lexer/q2c$(EXEEXT): $(top_srcdir)/src/language/lexer/q2c.c 
	@$(top_srcdir)/mkinstalldirs `dirname $@`
	$(CC) $(DEFAULT_INCLUDES) $(AM_CPPFLAGS) $(top_srcdir)/src/language/lexer/q2c.c -o $@


CLEANFILES += src/language/lexer/q2c$(EXEEXT)
