/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011 Free Software Foundation, Inc.

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

#ifndef TOKEN_H
#define TOKEN_H 1

#include <stdio.h>
#include "libpspp/str.h"
#include "data/identifier.h"

/* A PSPP syntax token.

   The 'type' member is used by the scanner (see scan.h) for SCAN_* values as
   well, which is why it is not declared as type "enum token_type". */
struct token
  {
    int type;                   /* Usually a "enum token_type" value. */
    double number;
    struct substring string;
  };

#define TOKEN_INITIALIZER(TYPE, NUMBER, STRING) \
        { TYPE, NUMBER, SS_LITERAL_INITIALIZER (STRING) }

void token_init (struct token *);
void token_destroy (struct token *);

char *token_to_string (const struct token *);

void token_print (const struct token *, FILE *);

#endif /* token.h */
