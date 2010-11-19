/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010 Free Software Foundation, Inc.

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

#ifndef DATA_IDENTIFIER_H
#define DATA_IDENTIFIER_H 1

#include <ctype.h>
#include <stdbool.h>
#include <unitypes.h>
#include "libpspp/str.h"

/* Token types. */
enum
  {
    T_ID = 256, /* Identifier. */
    T_POS_NUM,	/* Positive number. */
    T_NEG_NUM,	/* Negative number. */
    T_STRING,	/* Quoted string. */
    T_STOP,	/* End of input. */

    T_AND,	/* AND */
    T_OR,	/* OR */
    T_NOT,	/* NOT */

    T_EQ,	/* EQ */
    T_GE,	/* GE or >= */
    T_GT,	/* GT or > */
    T_LE,	/* LE or <= */
    T_LT,	/* LT or < */
    T_NE,	/* NE or ~= */

    T_ALL,	/* ALL */
    T_BY,	/* BY */
    T_TO,	/* TO */
    T_WITH,	/* WITH */

    T_EXP,	/* ** */
  };

/* Tokens. */
bool lex_is_keyword (int token);

/* Recognizing identifiers. */
bool lex_is_id1 (char);
bool lex_is_idn (char);
bool lex_uc_is_id1 (ucs4_t);
bool lex_uc_is_idn (ucs4_t);
bool lex_uc_is_space (ucs4_t);
size_t lex_id_get_length (struct substring);

/* Comparing identifiers. */
bool lex_id_match (struct substring keyword, struct substring token);
bool lex_id_match_n (struct substring keyword, struct substring token,
                     size_t n);
int lex_id_to_token (struct substring);

/* Identifier names. */
const char *lex_id_name (int);

#endif /* !data/identifier.h */
