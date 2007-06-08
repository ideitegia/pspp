/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <data/identifier.h>
#include <data/variable.h>
#include <libpspp/getl.h>

struct lexer;

/* Initialization. */
struct lexer * lex_create (struct source_stream *);
void lex_destroy (struct lexer *);

struct source_stream * lex_get_source_stream (const struct lexer *);


/* Common functions. */
void lex_get (struct lexer *);
void lex_error (struct lexer *, const char *, ...);
void lex_sbc_only_once (const char *);
void lex_sbc_missing (struct lexer *, const char *);
int lex_end_of_command (struct lexer *);

/* Token testing functions. */
bool lex_is_number (struct lexer *);
double lex_number (struct lexer *);
bool lex_is_integer (struct lexer *);
long lex_integer (struct lexer *);
bool lex_is_string (struct lexer *);


/* Token matching functions. */
bool lex_match (struct lexer *, int);
bool lex_match_id (struct lexer *, const char *);
bool lex_match_int (struct lexer *, int);

/* Forcible matching functions. */
bool lex_force_match (struct lexer *, int);
bool lex_force_match_id (struct lexer *, const char *);
bool lex_force_int (struct lexer *);
bool lex_force_num (struct lexer *);
bool lex_force_id (struct lexer *);
bool lex_force_string (struct lexer *);

/* Weird token functions. */
int lex_look_ahead (struct lexer *);
void lex_put_back (struct lexer *, int);
void lex_put_back_id (struct lexer *, const char *tokid);

/* Weird line processing functions. */
const char *lex_entire_line (const struct lexer *);
const struct string *lex_entire_line_ds (const struct lexer *);
const char *lex_rest_of_line (const struct lexer *);
bool lex_end_dot (const struct lexer *);
void lex_preprocess_line (struct string *, enum getl_syntax,
                          bool *line_starts_command,
                          bool *line_ends_command);
void lex_discard_line (struct lexer *);
void lex_discard_rest_of_command (struct lexer *);

/* Weird line reading functions. */
bool lex_get_line (struct lexer *);
bool lex_get_line_raw (struct lexer *, enum getl_syntax *);

/* Token names. */
const char *lex_token_name (int);
char *lex_token_representation (struct lexer *);

/* Token accessors */
int lex_token (const struct lexer *);
double lex_tokval (const struct lexer *);
const char *lex_tokid (const struct lexer *);
const struct string *lex_tokstr (const struct lexer *);

/* Really weird functions. */
void lex_negative_to_dash (struct lexer *);
void lex_skip_comment (struct lexer *);

#endif /* !lexer_h */
