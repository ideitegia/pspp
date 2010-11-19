/* PSPP - a program for statistical analysis.
   Copyright (C) 2010 Free Software Foundation, Inc.

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

#ifndef COMMAND_NAME_H
#define COMMAND_NAME_H 1

#include <stdbool.h>
#include "libpspp/str.h"

bool command_match (struct substring command, struct substring string,
                    bool *exact, int *missing_words);

/* Allows matching a string against a table of command names. */
struct command_matcher
  {
    struct substring string;
    bool extensible;
    void *exact_match;
    int n_matches;
    void *match;
    int match_missing_words;
  };

void command_matcher_init (struct command_matcher *, struct substring string);
void command_matcher_destroy (struct command_matcher *);

void command_matcher_add (struct command_matcher *, struct substring command,
                          void *aux);

void *command_matcher_get_match (const struct command_matcher *);
int command_matcher_get_missing_words (const struct command_matcher *);

#endif /* command-name.h */
