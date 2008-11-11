/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008  Free Software Foundation

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

#ifndef SRC_UI_COMMAND_LINE_H
#define SRC_UI_COMMAND_LINE_H

#include <argp.h>

struct command_line_processor;

struct command_line_processor * get_subject (struct argp_state *state);

struct command_line_processor *command_line_processor_create (const char *, const char *, void *);

void command_line_processor_add_options (struct command_line_processor *cla, const struct argp *child, const char *doc, void *aux);

void command_line_processor_replace_aux (struct command_line_processor *cla, const struct argp *child, void *aux);

void command_line_processor_destroy (struct command_line_processor *);

void command_line_processor_parse (struct command_line_processor *, int argc, char **argv);

#endif
