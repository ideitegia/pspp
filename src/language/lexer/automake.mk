## Process this file with automake to produce Makefile.in  -*- makefile -*-


language_lexer_sources = \
	src/language/lexer/command-name.c \
	src/language/lexer/command-name.h \
	src/language/lexer/include-path.c \
	src/language/lexer/include-path.h \
	src/language/lexer/lexer.c \
	src/language/lexer/lexer.h \
	src/language/lexer/subcommand-list.c  \
	src/language/lexer/subcommand-list.h \
	src/language/lexer/format-parser.c \
	src/language/lexer/format-parser.h \
	src/language/lexer/scan.c \
	src/language/lexer/scan.h \
	src/language/lexer/segment.c \
	src/language/lexer/segment.h \
	src/language/lexer/token.c \
	src/language/lexer/token.h \
	src/language/lexer/value-parser.c \
	src/language/lexer/value-parser.h \
	src/language/lexer/variable-parser.c \
	src/language/lexer/variable-parser.h

EXTRA_DIST += src/language/lexer/q2c.c


src/language/lexer/q2c$(EXEEXT_FOR_BUILD): $(top_srcdir)/src/language/lexer/q2c.c 
	@$(MKDIR_P) `dirname $@`
	$(AM_V_GEN)$(CC_FOR_BUILD) $(top_srcdir)/src/language/lexer/q2c.c -o $(top_builddir)/src/language/lexer/q2c$(EXEEXT_FOR_BUILD)


CLEANFILES += src/language/lexer/q2c$(EXEEXT_FOR_BUILD)
