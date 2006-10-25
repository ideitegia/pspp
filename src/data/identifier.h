/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.
   Written by Ben Pfaff <blp@gnu.org>.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#if !lex_def_h
#define lex_def_h 1

#include <ctype.h>
#include <stdbool.h>
#include <sys/types.h>

/* Token types. */
/* The order of the enumerals below is important.  Do not change it. */
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

    T_FIRST_KEYWORD = T_AND,
    T_LAST_KEYWORD = T_WITH,
    T_N_KEYWORDS = T_LAST_KEYWORD - T_FIRST_KEYWORD + 1
  };

/* Recognizing identifiers. */
bool lex_is_id1 (char);
bool lex_is_idn (char);
char *lex_skip_identifier (const char *);

/* Comparing identifiers. */
bool lex_id_match_len (const char *keyword_string, size_t keyword_len,
                       const char *token_string, size_t token_len);
bool lex_id_match (const char *keyword_string, const char *token_string);
int lex_id_to_token (const char *id, size_t len);

extern const char *const keywords[T_N_KEYWORDS + 1] ;

#endif /* !lex_def_h */
