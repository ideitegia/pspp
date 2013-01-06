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

#include <config.h>

#include "language/lexer/token.h"

#include <math.h>
#include <unictype.h>
#include <unistr.h>

#include "data/identifier.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/misc.h"


#include "gl/ftoastr.h"
#include "gl/xalloc.h"

/* Initializes TOKEN with an arbitrary type, number 0, and a null string. */
void
token_init (struct token *token)
{
  token->type = 0;
  token->number = 0.0;
  token->string = ss_empty ();
}

/* Frees the string that TOKEN contains. */
void
token_destroy (struct token *token)
{
  if (token != NULL)
    ss_dealloc (&token->string);
}

static char *
number_token_to_string (const struct token *token)
{
  char buffer[DBL_BUFSIZE_BOUND];

  c_dtoastr (buffer, sizeof buffer, 0, 0, fabs (token->number));
  return (token->type == T_POS_NUM
          ? xstrdup (buffer)
          : xasprintf ("-%s", buffer));
}

static char *
quoted_string_representation (struct substring ss, size_t n_quotes)
{
  char *rep;
  size_t i;
  char *p;

  p = rep = xmalloc (1 + ss.length + n_quotes + 1 + 1);
  *p++ = '\'';
  for (i = 0; i < ss.length; i++)
    {
      uint8_t c = ss.string[i];
      if (c == '\'')
        *p++ = c;
      *p++ = c;
    }
  *p++ = '\'';
  *p = '\0';

  return rep;
}

static char *
hex_string_representation (struct substring ss)
{
  char *rep;
  size_t i;
  char *p;

  p = rep = xmalloc (2 + 2 * ss.length + 1 + 1);
  *p++ = 'X';
  *p++ = '\'';
  for (i = 0; i < ss.length; i++)
    {
      static const char hex_digits[] = "0123456789abcdef";
      uint8_t c = ss.string[i];
      *p++ = hex_digits[c >> 4];
      *p++ = hex_digits[c & 15];
    }
  *p++ = '\'';
  *p = '\0';

  return rep;
}

static char *
string_representation (struct substring ss)
{
  size_t n_quotes;
  size_t ofs;
  int mblen;

  n_quotes = 0;
  for (ofs = 0; ofs < ss.length; ofs += mblen)
    {
      ucs4_t uc;

      mblen = u8_mbtoucr (&uc,
                          CHAR_CAST (const uint8_t *, ss.string + ofs),
                          ss.length - ofs);
      if (mblen < 0 || !uc_is_print (uc))
        return hex_string_representation (ss);
      else if (uc == '\'')
        n_quotes++;
    }
  return quoted_string_representation (ss, n_quotes);
}

/* Returns a UTF-8 string that would yield TOKEN if it appeared in a syntax
   file.  The caller should free the returned string, with free(), when it is
   no longer needed.

   The T_STOP token has no representation, so this function returns NULL. */
char *
token_to_string (const struct token *token)
{
  const char *name;

  switch (token->type)
    {
    case T_POS_NUM:
    case T_NEG_NUM:
      return number_token_to_string (token);

    case T_ID:
      return ss_xstrdup (token->string);

    case T_STRING:
      return string_representation (token->string);

    default:
      name = token_type_to_name (token->type);
      return name != NULL ? xstrdup (name) : NULL;
    }
}

/* Prints TOKEN on STREAM, for debugging. */
void
token_print (const struct token *token, FILE *stream)
{
  fputs (token_type_to_name (token->type), stream);
  if (token->type == T_POS_NUM || token->type == T_NEG_NUM
      || token->number != 0.0)
    {
      char s[DBL_BUFSIZE_BOUND];

      c_dtoastr (s, sizeof s, 0, 0, token->number);
      fprintf (stream, "\t%s", s);
    }
  if (token->type == T_ID || token->type == T_STRING || token->string.length)
    fprintf (stream, "\t\"%.*s\"",
             (int) token->string.length, token->string.string);
  putc ('\n', stream);
}
