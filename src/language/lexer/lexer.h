/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#ifndef LEXER_H
#define LEXER_H 1

#include <stdbool.h>
#include <stddef.h>

#include "data/identifier.h"
#include "data/variable.h"
#include "libpspp/compiler.h"
#include "libpspp/prompt.h"

struct lexer;

/* The syntax mode for which a syntax file is intended. */
enum lex_syntax_mode
  {
    LEX_SYNTAX_AUTO,            /* Try to guess intent. */
    LEX_SYNTAX_INTERACTIVE,     /* Interactive mode. */
    LEX_SYNTAX_BATCH            /* Batch mode. */
  };

/* Handling of errors. */
enum lex_error_mode
  {
    LEX_ERROR_TERMINAL,        /* Discard input line and continue reading. */
    LEX_ERROR_CONTINUE,        /* Continue to next command, except for
                                  cascading failures. */
    LEX_ERROR_STOP             /* Stop processing. */
  };

/* Reads a single syntax file as a stream of bytes encoded in UTF-8.

   Not opaque. */
struct lex_reader
  {
    const struct lex_reader_class *class;
    enum lex_syntax_mode syntax;
    enum lex_error_mode error;
    char *file_name;            /* NULL if not associated with a file. */
    int line_number;            /* 1-based initial line number, 0 if none. */
  };

/* An implementation of a lex_reader. */
struct lex_reader_class
  {
    /* Reads up to N bytes of data from READER into N.  Returns the positive
       number of bytes read if successful, or zero at end of input or on
       error.

       STYLE provides a hint to interactive readers as to what kind of syntax
       is being read right now. */
    size_t (*read) (struct lex_reader *reader, char *buf, size_t n,
                    enum prompt_style style);

    /* Closes and destroys READER, releasing any allocated storage.

       The caller will free the 'file_name' member of READER, so the
       implementation should not do so. */
    void (*destroy) (struct lex_reader *reader);
  };

/* Helper functions for lex_reader. */
void lex_reader_init (struct lex_reader *, const struct lex_reader_class *);
void lex_reader_set_file_name (struct lex_reader *, const char *file_name);

/* Creating various kinds of lex_readers. */
struct lex_reader *lex_reader_for_file (const char *file_name,
                                        const char *encoding,
                                        enum lex_syntax_mode syntax,
                                        enum lex_error_mode error);
struct lex_reader *lex_reader_for_string (const char *);
struct lex_reader *lex_reader_for_format (const char *, ...)
  PRINTF_FORMAT (1, 2);
struct lex_reader *lex_reader_for_substring_nocopy (struct substring);

/* Initialization. */
struct lexer *lex_create (void);
void lex_destroy (struct lexer *);

/* Files. */
void lex_include (struct lexer *, struct lex_reader *);
void lex_append (struct lexer *, struct lex_reader *);

/* Advancing. */
void lex_get (struct lexer *);

/* Token testing functions. */
bool lex_is_number (struct lexer *);
double lex_number (struct lexer *);
bool lex_is_integer (struct lexer *);
long lex_integer (struct lexer *);
bool lex_is_string (struct lexer *);

/* Token testing functions with lookahead. */
bool lex_next_is_number (struct lexer *, int n);
double lex_next_number (struct lexer *, int n);
bool lex_next_is_integer (struct lexer *, int n);
long lex_next_integer (struct lexer *, int n);
bool lex_next_is_string (struct lexer *, int n);

/* Token matching functions. */
bool lex_match (struct lexer *, enum token_type);
bool lex_match_id (struct lexer *, const char *);
bool lex_match_id_n (struct lexer *, const char *, size_t n);
bool lex_match_int (struct lexer *, int);
bool lex_match_phrase (struct lexer *, const char *s);

/* Forcible matching functions. */
bool lex_force_match (struct lexer *, enum token_type);
bool lex_force_match_id (struct lexer *, const char *);
bool lex_force_int (struct lexer *);
bool lex_force_num (struct lexer *);
bool lex_force_id (struct lexer *);
bool lex_force_string (struct lexer *);
bool lex_force_string_or_id (struct lexer *);

/* Token accessors. */
enum token_type lex_token (const struct lexer *);
double lex_tokval (const struct lexer *);
const char *lex_tokcstr (const struct lexer *);
struct substring lex_tokss (const struct lexer *);

/* Looking ahead. */
const struct token *lex_next (const struct lexer *, int n);
enum token_type lex_next_token (const struct lexer *, int n);
const char *lex_next_tokcstr (const struct lexer *, int n);
double lex_next_tokval (const struct lexer *, int n);
struct substring lex_next_tokss (const struct lexer *, int n);

/* Current position. */
int lex_get_first_line_number (const struct lexer *, int n);
int lex_get_last_line_number (const struct lexer *, int n);
int lex_get_first_column (const struct lexer *, int n);
int lex_get_last_column (const struct lexer *, int n);
const char *lex_get_file_name (const struct lexer *);

/* Issuing errors. */
void lex_error (struct lexer *, const char *, ...) PRINTF_FORMAT (2, 3);
void lex_next_error (struct lexer *, int n0, int n1, const char *, ...)
  PRINTF_FORMAT (4, 5);
int lex_end_of_command (struct lexer *);

void lex_error_expecting (struct lexer *, const char *, ...) SENTINEL(0);

void lex_sbc_only_once (const char *);
void lex_sbc_missing (const char *);

void lex_spec_only_once (struct lexer *, const char *subcommand,
                         const char *specification);
void lex_spec_missing (struct lexer *, const char *subcommand,
                       const char *specification);

void lex_error_valist (struct lexer *, const char *, va_list)
  PRINTF_FORMAT (2, 0);
void lex_next_error_valist (struct lexer *lexer, int n0, int n1,
                            const char *format, va_list)
  PRINTF_FORMAT (4, 0);

/* Error handling. */
enum lex_syntax_mode lex_get_syntax_mode (const struct lexer *);
enum lex_error_mode lex_get_error_mode (const struct lexer *);
void lex_discard_rest_of_command (struct lexer *);
void lex_interactive_reset (struct lexer *);
void lex_discard_noninteractive (struct lexer *);

#endif /* lexer.h */
