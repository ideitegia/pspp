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

#if !lexer_h
#define lexer_h 1

#include "var.h"
#include <ctype.h>
#include <stdbool.h>

/* Returns nonzero if character CH may be the first character in an
   identifier. */
#define CHAR_IS_ID1(CH)				\
	(isalpha ((unsigned char) (CH))		\
	 || (CH) == '@'				\
	 || (CH) == '#'				\
	 || (CH) == '$')

/* Returns nonzero if character CH may be a character in an
   identifier other than the first. */
#define CHAR_IS_IDN(CH)				\
	(CHAR_IS_ID1 (CH)			\
         || isdigit ((unsigned char) (CH))	\
	 || (CH) == '.'				\
	 || (CH) == '_')

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

extern int token;
extern double tokval;
extern char tokid[LONG_NAME_LEN + 1];
extern struct string tokstr;

#include <stddef.h>

/* Initialization. */
void lex_init (void);
void lex_done (void);

/* Common functions. */
void lex_get (void);
void lex_error (const char *, ...);
void lex_sbc_only_once (const char *);
void lex_sbc_missing (const char *);
int lex_end_of_command (void);

/* Token testing functions. */
bool lex_is_number (void);
double lex_number (void);
bool lex_is_integer (void);
long lex_integer (void);

/* Token matching functions. */
int lex_match (int);
int lex_match_id (const char *);
int lex_match_int (int);

/* Forcible matching functions. */
int lex_force_match (int);
int lex_force_match_id (const char *);
int lex_force_int (void);
int lex_force_num (void);
int lex_force_id (void);
int lex_force_string (void);

/* Comparing identifiers. */
int lex_id_match_len (const char *keyword_string, size_t keyword_len,
		      const char *token_string, size_t token_len);
int lex_id_match (const char *keyword_string, const char *token_string);
int lex_id_to_token (const char *id, size_t len);
	
/* Weird token functions. */
int lex_look_ahead (void);
void lex_put_back (int);
void lex_put_back_id (const char *tokid);

/* Weird line processing functions. */
const char *lex_entire_line (void);
const char *lex_rest_of_line (int *end_dot);
void lex_discard_line (void);
void lex_set_prog (char *p);

/* Weird line reading functions. */
int lex_get_line (void);
void lex_preprocess_line (void);

/* Token names. */
const char *lex_token_name (int);
char *lex_token_representation (void);

/* Really weird functions. */
void lex_negative_to_dash (void);
void lex_reset_eof (void);
void lex_skip_comment (void);

#endif /* !lexer_h */
