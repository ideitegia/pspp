## Process this file with automake to produce Makefile.in  -*- makefile -*-


language_lexer_sources = \
	src/language/lexer/lexer.c  src/language/lexer/lexer.h \
	src/language/lexer/subcommand-list.c  \
	src/language/lexer/subcommand-list.h \
	src/language/lexer/format-parser.c \
	src/language/lexer/format-parser.h \
	src/language/lexer/range-parser.c \
	src/language/lexer/range-parser.h \
	src/language/lexer/variable-parser.c \
	src/language/lexer/variable-parser.h

EXTRA_DIST += src/language/lexer/q2c.c


src/language/lexer/q2c$(EXEEXT_FOR_BUILD): $(top_srcdir)/src/language/lexer/q2c.c 
	@$(MKDIR_P) `dirname $@`
	$(CC_FOR_BUILD) $(top_srcdir)/src/language/lexer/q2c.c -o $(top_builddir)/src/language/lexer/q2c


CLEANFILES += src/language/lexer/q2c$(EXEEXT_FOR_BUILD)

EXTRA_DIST += src/language/lexer/OChangeLog
