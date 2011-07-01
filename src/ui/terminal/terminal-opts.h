/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010  Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */


#ifndef UI_TERMINAL_TERMINAL_OPTS_H
#define UI_TERMINAL_TERMINAL_OPTS_H 1

#include <stdbool.h>
#include "language/lexer/lexer.h"

struct argv_parser;
struct lexer;
struct terminal_opts;

struct terminal_opts *terminal_opts_init (struct argv_parser *,
                                          enum lex_syntax_mode *,
                                          bool *process_statrc,
                                          char **syntax_encoding);
void terminal_opts_done (struct terminal_opts *, int argc, char *argv[]);

#endif /* ui/terminal/terminal-opts.h */
