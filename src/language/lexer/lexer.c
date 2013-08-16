/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2009, 2010, 2011, 2013 Free Software Foundation, Inc.

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

#include <config.h>

#include "language/lexer/lexer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unictype.h>
#include <unistd.h>
#include <unistr.h>
#include <uniwidth.h>

#include "data/file-name.h"
#include "language/command.h"
#include "language/lexer/scan.h"
#include "language/lexer/segment.h"
#include "language/lexer/token.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/deque.h"
#include "libpspp/i18n.h"
#include "libpspp/ll.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"
#include "libpspp/u8-istream.h"
#include "output/journal.h"
#include "output/text-item.h"

#include "gl/c-ctype.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

/* A token within a lex_source. */
struct lex_token
  {
    /* The regular token information. */
    struct token token;

    /* Location of token in terms of the lex_source's buffer.
       src->tail <= line_pos <= token_pos <= src->head. */
    size_t token_pos;           /* Start of token. */
    size_t token_len;           /* Length of source for token in bytes. */
    size_t line_pos;            /* Start of line containing token_pos. */
    int first_line;             /* Line number at token_pos. */
  };

/* A source of tokens, corresponding to a syntax file.

   This is conceptually a lex_reader wrapped with everything needed to convert
   its UTF-8 bytes into tokens. */
struct lex_source
  {
    struct ll ll;               /* In lexer's list of sources. */
    struct lex_reader *reader;
    struct segmenter segmenter;
    bool eof;                   /* True if T_STOP was read from 'reader'. */

    /* Buffer of UTF-8 bytes. */
    char *buffer;
    size_t allocated;           /* Number of bytes allocated. */
    size_t tail;                /* &buffer[0] offset into UTF-8 source. */
    size_t head;                /* &buffer[head - tail] offset into source. */

    /* Positions in source file, tail <= pos <= head for each member here. */
    size_t journal_pos;         /* First byte not yet output to journal. */
    size_t seg_pos;             /* First byte not yet scanned as token. */
    size_t line_pos;            /* First byte of line containing seg_pos. */

    int n_newlines;             /* Number of new-lines up to seg_pos. */
    bool suppress_next_newline;

    /* Tokens. */
    struct deque deque;         /* Indexes into 'tokens'. */
    struct lex_token *tokens;   /* Lookahead tokens for parser. */
  };

static struct lex_source *lex_source_create (struct lex_reader *);
static void lex_source_destroy (struct lex_source *);

/* Lexer. */
struct lexer
  {
    struct ll_list sources;     /* Contains "struct lex_source"s. */
  };

static struct lex_source *lex_source__ (const struct lexer *);
static const struct lex_token *lex_next__ (const struct lexer *, int n);
static void lex_source_push_endcmd__ (struct lex_source *);

static void lex_source_pop__ (struct lex_source *);
static bool lex_source_get__ (const struct lex_source *);
static void lex_source_error_valist (struct lex_source *, int n0, int n1,
                                     const char *format, va_list)
   PRINTF_FORMAT (4, 0);
static const struct lex_token *lex_source_next__ (const struct lex_source *,
                                                  int n);

/* Initializes READER with the specified CLASS and otherwise some reasonable
   defaults.  The caller should fill in the others members as desired. */
void
lex_reader_init (struct lex_reader *reader,
                 const struct lex_reader_class *class)
{
  reader->class = class;
  reader->syntax = LEX_SYNTAX_AUTO;
  reader->error = LEX_ERROR_CONTINUE;
  reader->file_name = NULL;
  reader->line_number = 0;
}

/* Frees any file name already in READER and replaces it by a copy of
   FILE_NAME, or if FILE_NAME is null then clears any existing name. */
void
lex_reader_set_file_name (struct lex_reader *reader, const char *file_name)
{
  free (reader->file_name);
  reader->file_name = file_name != NULL ? xstrdup (file_name) : NULL;
}

/* Creates and returns a new lexer. */
struct lexer *
lex_create (void)
{
  struct lexer *lexer = xzalloc (sizeof *lexer);
  ll_init (&lexer->sources);
  return lexer;
}

/* Destroys LEXER. */
void
lex_destroy (struct lexer *lexer)
{
  if (lexer != NULL)
    {
      struct lex_source *source, *next;

      ll_for_each_safe (source, next, struct lex_source, ll, &lexer->sources)
        lex_source_destroy (source);
      free (lexer);
    }
}

/* Inserts READER into LEXER so that the next token read by LEXER comes from
   READER.  Before the caller, LEXER must either be empty or at a T_ENDCMD
   token. */
void
lex_include (struct lexer *lexer, struct lex_reader *reader)
{
  assert (ll_is_empty (&lexer->sources) || lex_token (lexer) == T_ENDCMD);
  ll_push_head (&lexer->sources, &lex_source_create (reader)->ll);
}

/* Appends READER to LEXER, so that it will be read after all other current
   readers have already been read. */
void
lex_append (struct lexer *lexer, struct lex_reader *reader)
{
  ll_push_tail (&lexer->sources, &lex_source_create (reader)->ll);
}

/* Advacning. */

static struct lex_token *
lex_push_token__ (struct lex_source *src)
{
  struct lex_token *token;

  if (deque_is_full (&src->deque))
    src->tokens = deque_expand (&src->deque, src->tokens, sizeof *src->tokens);

  token = &src->tokens[deque_push_front (&src->deque)];
  token_init (&token->token);
  return token;
}

static void
lex_source_pop__ (struct lex_source *src)
{
  token_destroy (&src->tokens[deque_pop_back (&src->deque)].token);
}

static void
lex_source_pop_front (struct lex_source *src)
{
  token_destroy (&src->tokens[deque_pop_front (&src->deque)].token);
}

/* Advances LEXER to the next token, consuming the current token. */
void
lex_get (struct lexer *lexer)
{
  struct lex_source *src;

  src = lex_source__ (lexer);
  if (src == NULL)
    return;

  if (!deque_is_empty (&src->deque))
    lex_source_pop__ (src);

  while (deque_is_empty (&src->deque))
    if (!lex_source_get__ (src))
      {
        lex_source_destroy (src);
        src = lex_source__ (lexer);
        if (src == NULL)
          return;
      }
}

/* Issuing errors. */

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_error (struct lexer *lexer, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  lex_next_error_valist (lexer, 0, 0, format, args);
  va_end (args);
}

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_error_valist (struct lexer *lexer, const char *format, va_list args)
{
  lex_next_error_valist (lexer, 0, 0, format, args);
}

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_next_error (struct lexer *lexer, int n0, int n1, const char *format, ...)
{
  va_list args;

  va_start (args, format);
  lex_next_error_valist (lexer, n0, n1, format, args);
  va_end (args);
}

/* Prints a syntax error message saying that OPTION0 or one of the other
   strings following it, up to the first NULL, is expected. */
void
lex_error_expecting (struct lexer *lexer, const char *option0, ...)
{
  enum { MAX_OPTIONS = 8 };
  const char *options[MAX_OPTIONS + 1];
  va_list args;
  int n;

  va_start (args, option0);
  options[0] = option0;
  n = 0;
  while (n + 1 < MAX_OPTIONS && options[n] != NULL)
    options[++n] = va_arg (args, const char *);
  va_end (args);

  switch (n)
    {
    case 0:
      lex_error (lexer, NULL);
      break;

    case 1:
      lex_error (lexer, _("expecting %s"), options[0]);
      break;

    case 2:
      lex_error (lexer, _("expecting %s or %s"), options[0], options[1]);
      break;

    case 3:
      lex_error (lexer, _("expecting %s, %s, or %s"), options[0], options[1],
                 options[2]);
      break;

    case 4:
      lex_error (lexer, _("expecting %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3]);
      break;

    case 5:
      lex_error (lexer, _("expecting %s, %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3], options[4]);
      break;

    case 6:
      lex_error (lexer, _("expecting %s, %s, %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5]);
      break;

    case 7:
      lex_error (lexer, _("expecting %s, %s, %s, %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5], options[6]);
      break;

    case 8:
      lex_error (lexer, _("expecting %s, %s, %s, %s, %s, %s, %s, or %s"),
                 options[0], options[1], options[2], options[3], options[4],
                 options[5], options[6], options[7]);
      break;

    default:
      NOT_REACHED ();
    }
}

/* Reports an error to the effect that subcommand SBC may only be specified
   once.

   This function does not take a lexer as an argument or use lex_error(),
   because the result would ordinarily just be redundant: "Syntax error at
   SUBCOMMAND: Subcommand SUBCOMMAND may only be specified once.", which does
   not help the user find the error. */
void
lex_sbc_only_once (const char *sbc)
{
  msg (SE, _("Subcommand %s may only be specified once."), sbc);
}

/* Reports an error to the effect that subcommand SBC is missing.

   This function does not take a lexer as an argument or use lex_error(),
   because a missing subcommand can normally be detected only after the whole
   command has been parsed, and so lex_error() would always report "Syntax
   error at end of command", which does not help the user find the error. */
void
lex_sbc_missing (const char *sbc)
{
  msg (SE, _("Required subcommand %s was not specified."), sbc);
}

/* Reports an error to the effect that specification SPEC may only be specified
   once within subcommand SBC. */
void
lex_spec_only_once (struct lexer *lexer, const char *sbc, const char *spec)
{
  lex_error (lexer, _("%s may only be specified once within subcommand %s"),
             spec, sbc);
}

/* Reports an error to the effect that specification SPEC is missing within
   subcommand SBC. */
void
lex_spec_missing (struct lexer *lexer, const char *sbc, const char *spec)
{
  lex_error (lexer, _("Required %s specification missing from %s subcommand"),
             sbc, spec);
}

/* Prints a syntax error message containing the current token and
   given message MESSAGE (if non-null). */
void
lex_next_error_valist (struct lexer *lexer, int n0, int n1,
                       const char *format, va_list args)
{
  struct lex_source *src = lex_source__ (lexer);

  if (src != NULL)
    lex_source_error_valist (src, n0, n1, format, args);
  else
    {
      struct string s;

      ds_init_empty (&s);
      ds_put_format (&s, _("Syntax error at end of input"));
      if (format != NULL)
        {
          ds_put_cstr (&s, ": ");
          ds_put_vformat (&s, format, args);
        }
      ds_put_byte (&s, '.');
      msg (SE, "%s", ds_cstr (&s));
      ds_destroy (&s);
    }
}

/* Checks that we're at end of command.
   If so, returns a successful command completion code.
   If not, flags a syntax error and returns an error command
   completion code. */
int
lex_end_of_command (struct lexer *lexer)
{
  if (lex_token (lexer) != T_ENDCMD && lex_token (lexer) != T_STOP)
    {
      lex_error (lexer, _("expecting end of command"));
      return CMD_FAILURE;
    }
  else
    return CMD_SUCCESS;
}

/* Token testing functions. */

/* Returns true if the current token is a number. */
bool
lex_is_number (struct lexer *lexer)
{
  return lex_next_is_number (lexer, 0);
}

/* Returns true if the current token is a string. */
bool
lex_is_string (struct lexer *lexer)
{
  return lex_next_is_string (lexer, 0);
}

/* Returns the value of the current token, which must be a
   floating point number. */
double
lex_number (struct lexer *lexer)
{
  return lex_next_number (lexer, 0);
}

/* Returns true iff the current token is an integer. */
bool
lex_is_integer (struct lexer *lexer)
{
  return lex_next_is_integer (lexer, 0);
}

/* Returns the value of the current token, which must be an
   integer. */
long
lex_integer (struct lexer *lexer)
{
  return lex_next_integer (lexer, 0);
}

/* Token testing functions with lookahead.

   A value of 0 for N as an argument to any of these functions refers to the
   current token.  Lookahead is limited to the current command.  Any N greater
   than the number of tokens remaining in the current command will be treated
   as referring to a T_ENDCMD token. */

/* Returns true if the token N ahead of the current token is a number. */
bool
lex_next_is_number (struct lexer *lexer, int n)
{
  enum token_type next_token = lex_next_token (lexer, n);
  return next_token == T_POS_NUM || next_token == T_NEG_NUM;
}

/* Returns true if the token N ahead of the current token is a string. */
bool
lex_next_is_string (struct lexer *lexer, int n)
{
  return lex_next_token (lexer, n) == T_STRING;
}

/* Returns the value of the token N ahead of the current token, which must be a
   floating point number. */
double
lex_next_number (struct lexer *lexer, int n)
{
  assert (lex_next_is_number (lexer, n));
  return lex_next_tokval (lexer, n);
}

/* Returns true if the token N ahead of the current token is an integer. */
bool
lex_next_is_integer (struct lexer *lexer, int n)
{
  double value;

  if (!lex_next_is_number (lexer, n))
    return false;

  value = lex_next_tokval (lexer, n);
  return value > LONG_MIN && value <= LONG_MAX && floor (value) == value;
}

/* Returns the value of the token N ahead of the current token, which must be
   an integer. */
long
lex_next_integer (struct lexer *lexer, int n)
{
  assert (lex_next_is_integer (lexer, n));
  return lex_next_tokval (lexer, n);
}

/* Token matching functions. */

/* If the current token has the specified TYPE, skips it and returns true.
   Otherwise, returns false. */
bool
lex_match (struct lexer *lexer, enum token_type type)
{
  if (lex_token (lexer) == type)
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* If the current token matches IDENTIFIER, skips it and returns true.
   IDENTIFIER may be abbreviated to its first three letters.  Otherwise,
   returns false.

   IDENTIFIER must be an ASCII string. */
bool
lex_match_id (struct lexer *lexer, const char *identifier)
{
  return lex_match_id_n (lexer, identifier, 3);
}

/* If the current token is IDENTIFIER, skips it and returns true.  IDENTIFIER
   may be abbreviated to its first N letters.  Otherwise, returns false.

   IDENTIFIER must be an ASCII string. */
bool
lex_match_id_n (struct lexer *lexer, const char *identifier, size_t n)
{
  if (lex_token (lexer) == T_ID
      && lex_id_match_n (ss_cstr (identifier), lex_tokss (lexer), n))
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* If the current token is integer X, skips it and returns true.  Otherwise,
   returns false. */
bool
lex_match_int (struct lexer *lexer, int x)
{
  if (lex_is_integer (lexer) && lex_integer (lexer) == x)
    {
      lex_get (lexer);
      return true;
    }
  else
    return false;
}

/* Forced matches. */

/* If this token is IDENTIFIER, skips it and returns true.  IDENTIFIER may be
   abbreviated to its first 3 letters.  Otherwise, reports an error and returns
   false.

   IDENTIFIER must be an ASCII string. */
bool
lex_force_match_id (struct lexer *lexer, const char *identifier)
{
  if (lex_match_id (lexer, identifier))
    return true;
  else
    {
      lex_error_expecting (lexer, identifier, NULL_SENTINEL);
      return false;
    }
}

/* If the current token has the specified TYPE, skips it and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_match (struct lexer *lexer, enum token_type type)
{
  if (lex_token (lexer) == type)
    {
      lex_get (lexer);
      return true;
    }
  else
    {
      char *s = xasprintf ("`%s'", token_type_to_string (type));
      lex_error_expecting (lexer, s, NULL_SENTINEL);
      free (s);
      return false;
    }
}

/* If the current token is a string, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_string (struct lexer *lexer)
{
  if (lex_is_string (lexer))
    return true;
  else
    {
      lex_error (lexer, _("expecting string"));
      return false;
    }
}

/* If the current token is a string or an identifier, does nothing and returns
   true.  Otherwise, reports an error and returns false.

   This is meant for use in syntactic situations where we want to encourage the
   user to supply a quoted string, but for compatibility we also accept
   identifiers.  (One example of such a situation is file names.)  Therefore,
   the error message issued when the current token is wrong only says that a
   string is expected and doesn't mention that an identifier would also be
   accepted. */
bool
lex_force_string_or_id (struct lexer *lexer)
{
  return lex_is_integer (lexer) || lex_force_string (lexer);
}

/* If the current token is an integer, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_int (struct lexer *lexer)
{
  if (lex_is_integer (lexer))
    return true;
  else
    {
      lex_error (lexer, _("expecting integer"));
      return false;
    }
}

/* If the current token is a number, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_num (struct lexer *lexer)
{
  if (lex_is_number (lexer))
    return true;

  lex_error (lexer, _("expecting number"));
  return false;
}

/* If the current token is an identifier, does nothing and returns true.
   Otherwise, reports an error and returns false. */
bool
lex_force_id (struct lexer *lexer)
{
  if (lex_token (lexer) == T_ID)
    return true;

  lex_error (lexer, _("expecting identifier"));
  return false;
}

/* Token accessors. */

/* Returns the type of LEXER's current token. */
enum token_type
lex_token (const struct lexer *lexer)
{
  return lex_next_token (lexer, 0);
}

/* Returns the number in LEXER's current token.

   Only T_NEG_NUM and T_POS_NUM tokens have meaningful values.  For other
   tokens this function will always return zero. */
double
lex_tokval (const struct lexer *lexer)
{
  return lex_next_tokval (lexer, 0);
}

/* Returns the null-terminated string in LEXER's current token, UTF-8 encoded.

   Only T_ID and T_STRING tokens have meaningful strings.  For other tokens
   this functions this function will always return NULL.

   The UTF-8 encoding of the returned string is correct for variable names and
   other identifiers.  Use filename_to_utf8() to use it as a filename.  Use
   data_in() to use it in a "union value".  */
const char *
lex_tokcstr (const struct lexer *lexer)
{
  return lex_next_tokcstr (lexer, 0);
}

/* Returns the string in LEXER's current token, UTF-8 encoded.  The string is
   null-terminated (but the null terminator is not included in the returned
   substring's 'length').

   Only T_ID and T_STRING tokens have meaningful strings.  For other tokens
   this functions this function will always return NULL.

   The UTF-8 encoding of the returned string is correct for variable names and
   other identifiers.  Use filename_to_utf8() to use it as a filename.  Use
   data_in() to use it in a "union value".  */
struct substring
lex_tokss (const struct lexer *lexer)
{
  return lex_next_tokss (lexer, 0);
}

/* Looking ahead.

   A value of 0 for N as an argument to any of these functions refers to the
   current token.  Lookahead is limited to the current command.  Any N greater
   than the number of tokens remaining in the current command will be treated
   as referring to a T_ENDCMD token. */

static const struct lex_token *
lex_next__ (const struct lexer *lexer_, int n)
{
  struct lexer *lexer = CONST_CAST (struct lexer *, lexer_);
  struct lex_source *src = lex_source__ (lexer);

  if (src != NULL)
    return lex_source_next__ (src, n);
  else
    {
      static const struct lex_token stop_token =
        { TOKEN_INITIALIZER (T_STOP, 0.0, ""), 0, 0, 0, 0 };

      return &stop_token;
    }
}

static const struct lex_token *
lex_source_next__ (const struct lex_source *src, int n)
{
  while (deque_count (&src->deque) <= n)
    {
      if (!deque_is_empty (&src->deque))
        {
          struct lex_token *front;

          front = &src->tokens[deque_front (&src->deque, 0)];
          if (front->token.type == T_STOP || front->token.type == T_ENDCMD)
            return front;
        }

      lex_source_get__ (src);
    }

  return &src->tokens[deque_back (&src->deque, n)];
}

/* Returns the "struct token" of the token N after the current one in LEXER.
   The returned pointer can be invalidated by pretty much any succeeding call
   into the lexer, although the string pointer within the returned token is
   only invalidated by consuming the token (e.g. with lex_get()). */
const struct token *
lex_next (const struct lexer *lexer, int n)
{
  return &lex_next__ (lexer, n)->token;
}

/* Returns the type of the token N after the current one in LEXER. */
enum token_type
lex_next_token (const struct lexer *lexer, int n)
{
  return lex_next (lexer, n)->type;
}

/* Returns the number in the tokn N after the current one in LEXER.

   Only T_NEG_NUM and T_POS_NUM tokens have meaningful values.  For other
   tokens this function will always return zero. */
double
lex_next_tokval (const struct lexer *lexer, int n)
{
  const struct token *token = lex_next (lexer, n);
  return token->number;
}

/* Returns the null-terminated string in the token N after the current one, in
   UTF-8 encoding.

   Only T_ID and T_STRING tokens have meaningful strings.  For other tokens
   this functions this function will always return NULL.

   The UTF-8 encoding of the returned string is correct for variable names and
   other identifiers.  Use filename_to_utf8() to use it as a filename.  Use
   data_in() to use it in a "union value".  */
const char *
lex_next_tokcstr (const struct lexer *lexer, int n)
{
  return lex_next_tokss (lexer, n).string;
}

/* Returns the string in the token N after the current one, in UTF-8 encoding.
   The string is null-terminated (but the null terminator is not included in
   the returned substring's 'length').

   Only T_ID and T_STRING tokens have meaningful strings.  For other tokens
   this functions this function will always return NULL.

   The UTF-8 encoding of the returned string is correct for variable names and
   other identifiers.  Use filename_to_utf8() to use it as a filename.  Use
   data_in() to use it in a "union value".  */
struct substring
lex_next_tokss (const struct lexer *lexer, int n)
{
  return lex_next (lexer, n)->string;
}

static bool
lex_tokens_match (const struct token *actual, const struct token *expected)
{
  if (actual->type != expected->type)
    return false;

  switch (actual->type)
    {
    case T_POS_NUM:
    case T_NEG_NUM:
      return actual->number == expected->number;

    case T_ID:
      return lex_id_match (expected->string, actual->string);

    case T_STRING:
      return (actual->string.length == expected->string.length
              && !memcmp (actual->string.string, expected->string.string,
                          actual->string.length));

    default:
      return true;
    }
}

/* If LEXER is positioned at the sequence of tokens that may be parsed from S,
   skips it and returns true.  Otherwise, returns false.

   S may consist of an arbitrary sequence of tokens, e.g. "KRUSKAL-WALLIS",
   "2SLS", or "END INPUT PROGRAM".  Identifiers may be abbreviated to their
   first three letters. */
bool
lex_match_phrase (struct lexer *lexer, const char *s)
{
  struct string_lexer slex;
  struct token token;
  int i;

  i = 0;
  string_lexer_init (&slex, s, SEG_MODE_INTERACTIVE);
  while (string_lexer_next (&slex, &token))
    if (token.type != SCAN_SKIP)
      {
        bool match = lex_tokens_match (lex_next (lexer, i++), &token);
        token_destroy (&token);
        if (!match)
          return false;
      }

  while (i-- > 0)
    lex_get (lexer);
  return true;
}

static int
lex_source_get_first_line_number (const struct lex_source *src, int n)
{
  return lex_source_next__ (src, n)->first_line;
}

static int
count_newlines (char *s, size_t length)
{
  int n_newlines = 0;
  char *newline;

  while ((newline = memchr (s, '\n', length)) != NULL)
    {
      n_newlines++;
      length -= (newline + 1) - s;
      s = newline + 1;
    }

  return n_newlines;
}

static int
lex_source_get_last_line_number (const struct lex_source *src, int n)
{
  const struct lex_token *token = lex_source_next__ (src, n);

  if (token->first_line == 0)
    return 0;
  else
    {
      char *token_str = &src->buffer[token->token_pos - src->tail];
      return token->first_line + count_newlines (token_str, token->token_len) + 1;
    }
}

static int
count_columns (const char *s_, size_t length)
{
  const uint8_t *s = CHAR_CAST (const uint8_t *, s_);
  int columns;
  size_t ofs;
  int mblen;

  columns = 0;
  for (ofs = 0; ofs < length; ofs += mblen)
    {
      ucs4_t uc;

      mblen = u8_mbtouc (&uc, s + ofs, length - ofs);
      if (uc != '\t')
        {
          int width = uc_width (uc, "UTF-8");
          if (width > 0)
            columns += width;
        }
      else
        columns = ROUND_UP (columns + 1, 8);
    }

  return columns + 1;
}

static int
lex_source_get_first_column (const struct lex_source *src, int n)
{
  const struct lex_token *token = lex_source_next__ (src, n);
  return count_columns (&src->buffer[token->line_pos - src->tail],
                        token->token_pos - token->line_pos);
}

static int
lex_source_get_last_column (const struct lex_source *src, int n)
{
  const struct lex_token *token = lex_source_next__ (src, n);
  char *start, *end, *newline;

  start = &src->buffer[token->line_pos - src->tail];
  end = &src->buffer[(token->token_pos + token->token_len) - src->tail];
  newline = memrchr (start, '\n', end - start);
  if (newline != NULL)
    start = newline + 1;
  return count_columns (start, end - start);
}

/* Returns the 1-based line number of the start of the syntax that represents
   the token N after the current one in LEXER.  Returns 0 for a T_STOP token or
   if the token is drawn from a source that does not have line numbers. */
int
lex_get_first_line_number (const struct lexer *lexer, int n)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src != NULL ? lex_source_get_first_line_number (src, n) : 0;
}

/* Returns the 1-based line number of the end of the syntax that represents the
   token N after the current one in LEXER, plus 1.  Returns 0 for a T_STOP
   token or if the token is drawn from a source that does not have line
   numbers.

   Most of the time, a single token is wholly within a single line of syntax,
   but there are two exceptions: a T_STRING token can be made up of multiple
   segments on adjacent lines connected with "+" punctuators, and a T_NEG_NUM
   token can consist of a "-" on one line followed by the number on the next.
 */
int
lex_get_last_line_number (const struct lexer *lexer, int n)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src != NULL ? lex_source_get_last_line_number (src, n) : 0;
}

/* Returns the 1-based column number of the start of the syntax that represents
   the token N after the current one in LEXER.  Returns 0 for a T_STOP
   token.

   Column numbers are measured according to the width of characters as shown in
   a typical fixed-width font, in which CJK characters have width 2 and
   combining characters have width 0.  */
int
lex_get_first_column (const struct lexer *lexer, int n)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src != NULL ? lex_source_get_first_column (src, n) : 0;
}

/* Returns the 1-based column number of the end of the syntax that represents
   the token N after the current one in LEXER, plus 1.  Returns 0 for a T_STOP
   token.

   Column numbers are measured according to the width of characters as shown in
   a typical fixed-width font, in which CJK characters have width 2 and
   combining characters have width 0.  */
int
lex_get_last_column (const struct lexer *lexer, int n)
{
  const struct lex_source *src = lex_source__ (lexer);
  return src != NULL ? lex_source_get_last_column (src, n) : 0;
}

/* Returns the name of the syntax file from which the current command is drawn.
   Returns NULL for a T_STOP token or if the command's source does not have
   line numbers.

   There is no version of this function that takes an N argument because
   lookahead only works to the end of a command and any given command is always
   within a single syntax file. */
const char *
lex_get_file_name (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  return src == NULL ? NULL : src->reader->file_name;
}

/* Returns the syntax mode for the syntax file from which the current drawn is
   drawn.  Returns LEX_SYNTAX_AUTO for a T_STOP token or if the command's
   source does not have line numbers.

   There is no version of this function that takes an N argument because
   lookahead only works to the end of a command and any given command is always
   within a single syntax file. */
enum lex_syntax_mode
lex_get_syntax_mode (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  return src == NULL ? LEX_SYNTAX_AUTO : src->reader->syntax;
}

/* Returns the error mode for the syntax file from which the current drawn is
   drawn.  Returns LEX_ERROR_TERMINAL for a T_STOP token or if the command's
   source does not have line numbers.

   There is no version of this function that takes an N argument because
   lookahead only works to the end of a command and any given command is always
   within a single syntax file. */
enum lex_error_mode
lex_get_error_mode (const struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  return src == NULL ? LEX_ERROR_TERMINAL : src->reader->error;
}

/* If the source that LEXER is currently reading has error mode
   LEX_ERROR_TERMINAL, discards all buffered input and tokens, so that the next
   token to be read comes directly from whatever is next read from the stream.

   It makes sense to call this function after encountering an error in a
   command entered on the console, because usually the user would prefer not to
   have cascading errors. */
void
lex_interactive_reset (struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);
  if (src != NULL && src->reader->error == LEX_ERROR_TERMINAL)
    {
      src->head = src->tail = 0;
      src->journal_pos = src->seg_pos = src->line_pos = 0;
      src->n_newlines = 0;
      src->suppress_next_newline = false;
      segmenter_init (&src->segmenter, segmenter_get_mode (&src->segmenter));
      while (!deque_is_empty (&src->deque))
        lex_source_pop__ (src);
      lex_source_push_endcmd__ (src);
    }
}

/* Advances past any tokens in LEXER up to a T_ENDCMD or T_STOP. */
void
lex_discard_rest_of_command (struct lexer *lexer)
{
  while (lex_token (lexer) != T_STOP && lex_token (lexer) != T_ENDCMD)
    lex_get (lexer);
}

/* Discards all lookahead tokens in LEXER, then discards all input sources
   until it encounters one with error mode LEX_ERROR_TERMINAL or until it
   runs out of input sources. */
void
lex_discard_noninteractive (struct lexer *lexer)
{
  struct lex_source *src = lex_source__ (lexer);

  if (src != NULL)
    {
      while (!deque_is_empty (&src->deque))
        lex_source_pop__ (src);

      for (; src != NULL && src->reader->error != LEX_ERROR_TERMINAL;
           src = lex_source__ (lexer))
        lex_source_destroy (src);
    }
}

static size_t
lex_source_max_tail__ (const struct lex_source *src)
{
  const struct lex_token *token;
  size_t max_tail;

  assert (src->seg_pos >= src->line_pos);
  max_tail = MIN (src->journal_pos, src->line_pos);

  /* Use the oldest token also.  (We know that src->deque cannot be empty
     because we are in the process of adding a new token, which is already
     initialized enough to use here.) */
  token = &src->tokens[deque_back (&src->deque, 0)];
  assert (token->token_pos >= token->line_pos);
  max_tail = MIN (max_tail, token->line_pos);

  return max_tail;
}

static void
lex_source_expand__ (struct lex_source *src)
{
  if (src->head - src->tail >= src->allocated)
    {
      size_t max_tail = lex_source_max_tail__ (src);
      if (max_tail > src->tail)
        {
          /* Advance the tail, freeing up room at the head. */
          memmove (src->buffer, src->buffer + (max_tail - src->tail),
                   src->head - max_tail);
          src->tail = max_tail;
        }
      else
        {
          /* Buffer is completely full.  Expand it. */
          src->buffer = x2realloc (src->buffer, &src->allocated);
        }
    }
  else
    {
      /* There's space available at the head of the buffer.  Nothing to do. */
    }
}

static void
lex_source_read__ (struct lex_source *src)
{
  do
    {
      size_t head_ofs;
      size_t space;
      size_t n;

      lex_source_expand__ (src);

      head_ofs = src->head - src->tail;
      space = src->allocated - head_ofs;
      n = src->reader->class->read (src->reader, &src->buffer[head_ofs],
                                    space,
                                    segmenter_get_prompt (&src->segmenter));
      assert (n <= space);

      if (n == 0)
        {
          /* End of input.

             Ensure that the input always ends in a new-line followed by a null
             byte, as required by the segmenter library. */

          if (src->head == src->tail
              || src->buffer[src->head - src->tail - 1] != '\n')
            src->buffer[src->head++ - src->tail] = '\n';

          lex_source_expand__ (src);
          src->buffer[src->head++ - src->tail] = '\0';

          return;
        }

      src->head += n;
    }
  while (!memchr (&src->buffer[src->seg_pos - src->tail], '\n',
                  src->head - src->seg_pos));
}

static struct lex_source *
lex_source__ (const struct lexer *lexer)
{
  return (ll_is_empty (&lexer->sources) ? NULL
          : ll_data (ll_head (&lexer->sources), struct lex_source, ll));
}

static struct substring
lex_source_get_syntax__ (const struct lex_source *src, int n0, int n1)
{
  const struct lex_token *token0 = lex_source_next__ (src, n0);
  const struct lex_token *token1 = lex_source_next__ (src, MAX (n0, n1));
  size_t start = token0->token_pos;
  size_t end = token1->token_pos + token1->token_len;

  return ss_buffer (&src->buffer[start - src->tail], end - start);
}

static void
lex_ellipsize__ (struct substring in, char *out, size_t out_size)
{
  size_t out_maxlen;
  size_t out_len;
  int mblen;

  assert (out_size >= 16);
  out_maxlen = out_size - (in.length >= out_size ? 3 : 0) - 1;
  for (out_len = 0; out_len < in.length; out_len += mblen)
    {
      if (in.string[out_len] == '\n'
          || (in.string[out_len] == '\r'
              && out_len + 1 < in.length
              && in.string[out_len + 1] == '\n'))
        break;

      mblen = u8_mblen (CHAR_CAST (const uint8_t *, in.string + out_len),
                        in.length - out_len);
      if (out_len + mblen > out_maxlen)
        break;
    }

  memcpy (out, in.string, out_len);
  strcpy (&out[out_len], out_len < in.length ? "..." : "");
}

static void
lex_source_error_valist (struct lex_source *src, int n0, int n1,
                         const char *format, va_list args)
{
  const struct lex_token *token;
  struct string s;
  struct msg m;

  ds_init_empty (&s);

  token = lex_source_next__ (src, n0);
  if (token->token.type == T_ENDCMD)
    ds_put_cstr (&s, _("Syntax error at end of command"));
  else
    {
      struct substring syntax = lex_source_get_syntax__ (src, n0, n1);
      if (!ss_is_empty (syntax))
        {
          char syntax_cstr[64];

          lex_ellipsize__ (syntax, syntax_cstr, sizeof syntax_cstr);
          ds_put_format (&s, _("Syntax error at `%s'"), syntax_cstr);
        }
      else
        ds_put_cstr (&s, _("Syntax error"));
    }

  if (format)
    {
      ds_put_cstr (&s, ": ");
      ds_put_vformat (&s, format, args);
    }
  ds_put_byte (&s, '.');

  m.category = MSG_C_SYNTAX;
  m.severity = MSG_S_ERROR;
  m.file_name = src->reader->file_name;
  m.first_line = lex_source_get_first_line_number (src, n0);
  m.last_line = lex_source_get_last_line_number (src, n1);
  m.first_column = lex_source_get_first_column (src, n0);
  m.last_column = lex_source_get_last_column (src, n1);
  m.text = ds_steal_cstr (&s);
  msg_emit (&m);
}

static void PRINTF_FORMAT (2, 3)
lex_get_error (struct lex_source *src, const char *format, ...)
{
  va_list args;
  int n;

  va_start (args, format);

  n = deque_count (&src->deque) - 1;
  lex_source_error_valist (src, n, n, format, args);
  lex_source_pop_front (src);

  va_end (args);
}

static bool
lex_source_get__ (const struct lex_source *src_)
{
  struct lex_source *src = CONST_CAST (struct lex_source *, src_);

  struct state
    {
      struct segmenter segmenter;
      enum segment_type last_segment;
      int newlines;
      size_t line_pos;
      size_t seg_pos;
    };

  struct state state, saved;
  enum scan_result result;
  struct scanner scanner;
  struct lex_token *token;
  int n_lines;
  int i;

  if (src->eof)
    return false;

  state.segmenter = src->segmenter;
  state.newlines = 0;
  state.seg_pos = src->seg_pos;
  state.line_pos = src->line_pos;
  saved = state;

  token = lex_push_token__ (src);
  scanner_init (&scanner, &token->token);
  token->line_pos = src->line_pos;
  token->token_pos = src->seg_pos;
  if (src->reader->line_number > 0)
    token->first_line = src->reader->line_number + src->n_newlines;
  else
    token->first_line = 0;

  for (;;)
    {
      enum segment_type type;
      const char *segment;
      size_t seg_maxlen;
      int seg_len;

      segment = &src->buffer[state.seg_pos - src->tail];
      seg_maxlen = src->head - state.seg_pos;
      seg_len = segmenter_push (&state.segmenter, segment, seg_maxlen, &type);
      if (seg_len < 0)
        {
          lex_source_read__ (src);
          continue;
        }

      state.last_segment = type;
      state.seg_pos += seg_len;
      if (type == SEG_NEWLINE)
        {
          state.newlines++;
          state.line_pos = state.seg_pos;
        }

      result = scanner_push (&scanner, type, ss_buffer (segment, seg_len),
                             &token->token);
      if (result == SCAN_SAVE)
        saved = state;
      else if (result == SCAN_BACK)
        {
          state = saved;
          break;
        }
      else if (result == SCAN_DONE)
        break;
    }

  n_lines = state.newlines;
  if (state.last_segment == SEG_END_COMMAND && !src->suppress_next_newline)
    {
      n_lines++;
      src->suppress_next_newline = true;
    }
  else if (n_lines > 0 && src->suppress_next_newline)
    {
      n_lines--;
      src->suppress_next_newline = false;
    }
  for (i = 0; i < n_lines; i++)
    {
      const char *newline;
      const char *line;
      size_t line_len;
      char *syntax;

      line = &src->buffer[src->journal_pos - src->tail];
      newline = rawmemchr (line, '\n');
      line_len = newline - line;
      if (line_len > 0 && line[line_len - 1] == '\r')
        line_len--;

      syntax = malloc (line_len + 2);
      memcpy (syntax, line, line_len);
      syntax[line_len] = '\n';
      syntax[line_len + 1] = '\0';

      text_item_submit (text_item_create_nocopy (TEXT_ITEM_SYNTAX, syntax));

      src->journal_pos += newline - line + 1;
    }

  token->token_len = state.seg_pos - src->seg_pos;

  src->segmenter = state.segmenter;
  src->seg_pos = state.seg_pos;
  src->line_pos = state.line_pos;
  src->n_newlines += state.newlines;

  switch (token->token.type)
    {
    default:
      break;

    case T_STOP:
      token->token.type = T_ENDCMD;
      src->eof = true;
      break;

    case SCAN_BAD_HEX_LENGTH:
      lex_get_error (src, _("String of hex digits has %d characters, which "
                            "is not a multiple of 2"),
                     (int) token->token.number);
      break;

    case SCAN_BAD_HEX_DIGIT:
    case SCAN_BAD_UNICODE_DIGIT:
      lex_get_error (src, _("`%c' is not a valid hex digit"),
                     (int) token->token.number);
      break;

    case SCAN_BAD_UNICODE_LENGTH:
      lex_get_error (src, _("Unicode string contains %d bytes, which is "
                            "not in the valid range of 1 to 8 bytes"),
                     (int) token->token.number);
      break;

    case SCAN_BAD_UNICODE_CODE_POINT:
      lex_get_error (src, _("U+%04X is not a valid Unicode code point"),
                     (int) token->token.number);
      break;

    case SCAN_EXPECTED_QUOTE:
      lex_get_error (src, _("Unterminated string constant"));
      break;

    case SCAN_EXPECTED_EXPONENT:
      lex_get_error (src, _("Missing exponent following `%s'"),
                     token->token.string.string);
      break;

    case SCAN_UNEXPECTED_DOT:
      lex_get_error (src, _("Unexpected `.' in middle of command"));
      break;

    case SCAN_UNEXPECTED_CHAR:
      {
        char c_name[16];
        lex_get_error (src, _("Bad character %s in input"),
                       uc_name (token->token.number, c_name));
      }
      break;

    case SCAN_SKIP:
      lex_source_pop_front (src);
      break;
    }

  return true;
}

static void
lex_source_push_endcmd__ (struct lex_source *src)
{
  struct lex_token *token = lex_push_token__ (src);
  token->token.type = T_ENDCMD;
  token->token_pos = 0;
  token->token_len = 0;
  token->line_pos = 0;
  token->first_line = 0;
}

static struct lex_source *
lex_source_create (struct lex_reader *reader)
{
  struct lex_source *src;
  enum segmenter_mode mode;

  src = xzalloc (sizeof *src);
  src->reader = reader;

  if (reader->syntax == LEX_SYNTAX_AUTO)
    mode = SEG_MODE_AUTO;
  else if (reader->syntax == LEX_SYNTAX_INTERACTIVE)
    mode = SEG_MODE_INTERACTIVE;
  else if (reader->syntax == LEX_SYNTAX_BATCH)
    mode = SEG_MODE_BATCH;
  else
    NOT_REACHED ();
  segmenter_init (&src->segmenter, mode);

  src->tokens = deque_init (&src->deque, 4, sizeof *src->tokens);

  lex_source_push_endcmd__ (src);

  return src;
}

static void
lex_source_destroy (struct lex_source *src)
{
  char *file_name = src->reader->file_name;
  if (src->reader->class->destroy != NULL)
    src->reader->class->destroy (src->reader);
  free (file_name);
  free (src->buffer);
  while (!deque_is_empty (&src->deque))
    lex_source_pop__ (src);
  free (src->tokens);
  ll_remove (&src->ll);
  free (src);
}

struct lex_file_reader
  {
    struct lex_reader reader;
    struct u8_istream *istream;
    char *file_name;
  };

static struct lex_reader_class lex_file_reader_class;

/* Creates and returns a new lex_reader that will read from file FILE_NAME (or
   from stdin if FILE_NAME is "-").  The file is expected to be encoded with
   ENCODING, which should take one of the forms accepted by
   u8_istream_for_file().  SYNTAX and ERROR become the syntax mode and error
   mode of the new reader, respectively.

   Returns a null pointer if FILE_NAME cannot be opened. */
struct lex_reader *
lex_reader_for_file (const char *file_name, const char *encoding,
                     enum lex_syntax_mode syntax,
                     enum lex_error_mode error)
{
  struct lex_file_reader *r;
  struct u8_istream *istream;

  istream = (!strcmp(file_name, "-")
             ? u8_istream_for_fd (encoding, STDIN_FILENO)
             : u8_istream_for_file (encoding, file_name, O_RDONLY));
  if (istream == NULL)
    {
      msg (ME, _("Opening `%s': %s."), file_name, strerror (errno));
      return NULL;
    }

  r = xmalloc (sizeof *r);
  lex_reader_init (&r->reader, &lex_file_reader_class);
  r->reader.syntax = syntax;
  r->reader.error = error;
  r->reader.file_name = xstrdup (file_name);
  r->reader.line_number = 1;
  r->istream = istream;
  r->file_name = xstrdup (file_name);

  return &r->reader;
}

static struct lex_file_reader *
lex_file_reader_cast (struct lex_reader *r)
{
  return UP_CAST (r, struct lex_file_reader, reader);
}

static size_t
lex_file_read (struct lex_reader *r_, char *buf, size_t n,
               enum prompt_style prompt_style UNUSED)
{
  struct lex_file_reader *r = lex_file_reader_cast (r_);
  ssize_t n_read = u8_istream_read (r->istream, buf, n);
  if (n_read < 0)
    {
      msg (ME, _("Error reading `%s': %s."), r->file_name, strerror (errno));
      return 0;
    }
  return n_read;
}

static void
lex_file_close (struct lex_reader *r_)
{
  struct lex_file_reader *r = lex_file_reader_cast (r_);

  if (u8_istream_fileno (r->istream) != STDIN_FILENO)
    {
      if (u8_istream_close (r->istream) != 0)
        msg (ME, _("Error closing `%s': %s."), r->file_name, strerror (errno));
    }
  else
    u8_istream_free (r->istream);

  free (r->file_name);
  free (r);
}

static struct lex_reader_class lex_file_reader_class =
  {
    lex_file_read,
    lex_file_close
  };

struct lex_string_reader
  {
    struct lex_reader reader;
    struct substring s;
    size_t offset;
  };

static struct lex_reader_class lex_string_reader_class;

/* Creates and returns a new lex_reader for the contents of S, which must be
   encoded in UTF-8.  The new reader takes ownership of S and will free it
   with ss_dealloc() when it is closed. */
struct lex_reader *
lex_reader_for_substring_nocopy (struct substring s)
{
  struct lex_string_reader *r;

  r = xmalloc (sizeof *r);
  lex_reader_init (&r->reader, &lex_string_reader_class);
  r->reader.syntax = LEX_SYNTAX_AUTO;
  r->s = s;
  r->offset = 0;

  return &r->reader;
}

/* Creates and returns a new lex_reader for a copy of null-terminated string S,
   which must be encoded in UTF-8.  The caller retains ownership of S. */
struct lex_reader *
lex_reader_for_string (const char *s)
{
  struct substring ss;
  ss_alloc_substring (&ss, ss_cstr (s));
  return lex_reader_for_substring_nocopy (ss);
}

/* Formats FORMAT as a printf()-like format string and creates and returns a
   new lex_reader for the formatted result.  */
struct lex_reader *
lex_reader_for_format (const char *format, ...)
{
  struct lex_reader *r;
  va_list args;

  va_start (args, format);
  r = lex_reader_for_substring_nocopy (ss_cstr (xvasprintf (format, args)));
  va_end (args);

  return r;
}

static struct lex_string_reader *
lex_string_reader_cast (struct lex_reader *r)
{
  return UP_CAST (r, struct lex_string_reader, reader);
}

static size_t
lex_string_read (struct lex_reader *r_, char *buf, size_t n,
                 enum prompt_style prompt_style UNUSED)
{
  struct lex_string_reader *r = lex_string_reader_cast (r_);
  size_t chunk;

  chunk = MIN (n, r->s.length - r->offset);
  memcpy (buf, r->s.string + r->offset, chunk);
  r->offset += chunk;

  return chunk;
}

static void
lex_string_close (struct lex_reader *r_)
{
  struct lex_string_reader *r = lex_string_reader_cast (r_);

  ss_dealloc (&r->s);
  free (r);
}

static struct lex_reader_class lex_string_reader_class =
  {
    lex_string_read,
    lex_string_close
  };
