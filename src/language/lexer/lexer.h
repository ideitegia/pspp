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

#include <data/variable.h>
#include <ctype.h>
#include <stdbool.h>

#include <data/identifier.h>


extern int token;
extern double tokval;
extern char tokid[LONG_NAME_LEN + 1];
extern struct string tokstr;

#include <stddef.h>

/* Initialization. */
void lex_init (bool (*)(struct string *, bool*));
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
bool lex_match (int);
bool lex_match_id (const char *);
bool lex_match_int (int);

/* Forcible matching functions. */
bool lex_force_match (int);
bool lex_force_match_id (const char *);
bool lex_force_int (void);
bool lex_force_num (void);
bool lex_force_id (void);
bool lex_force_string (void);
	
/* Weird token functions. */
int lex_look_ahead (void);
void lex_put_back (int);
void lex_put_back_id (const char *tokid);

/* Weird line processing functions. */
const char *lex_entire_line (void);
const struct string *lex_entire_line_ds (void);
const char *lex_rest_of_line (int *end_dot);
void lex_discard_line (void);
void lex_discard_rest_of_command (void);

/* Weird line reading functions. */
bool lex_get_line (void);
bool lex_get_line_raw (void);

/* Token names. */
const char *lex_token_name (int);
char *lex_token_representation (void);

/* Really weird functions. */
void lex_negative_to_dash (void);
void lex_reset_eof (void);
void lex_skip_comment (void);

#endif /* !lexer_h */
