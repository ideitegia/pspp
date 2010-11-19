/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2005, 2009, 2010 Free Software Foundation, Inc.

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

/*
   This file is concerned with the definition of the PSPP syntax, NOT the
   action of scanning/parsing code .
*/

#include <config.h>

#include "data/identifier.h"

#include <assert.h>
#include <string.h>
#include <unictype.h>

#include "libpspp/assertion.h"

#include "gl/c-ctype.h"

/* Recognizing identifiers. */

static bool
is_ascii_id1 (unsigned char c)
{
  return c_isalpha (c) || c == '@' || c == '#' || c == '$';
}

static bool
is_ascii_idn (unsigned char c)
{
  return is_ascii_id1 (c) || isdigit (c) || c == '.' || c == '_';
}

/* Returns true if C may be the first byte in an identifier in the current
   locale.

   (PSPP is transitioning to using Unicode internally for syntax, so please
   use lex_uc_is_id1() instead, if possible.) */
bool
lex_is_id1 (char c)
{
  return is_ascii_id1 (c) || (unsigned char) c >= 128;
}

/* Returns true if C may be a byte in an identifier other than the first.

   (PSPP is transitioning to using Unicode internally for syntax, so please
   use lex_uc_is_idn() instead, if possible.) */
bool
lex_is_idn (char c)
{
  return is_ascii_idn (c) || (unsigned char) c >= 128;
}

/* Returns true if Unicode code point UC may be the first character in an
   identifier in the current locale. */
bool
lex_uc_is_id1 (ucs4_t uc)
{
  return is_ascii_id1 (uc) || (uc >= 0x80 && uc_is_property_id_start (uc));
}

/* Returns true if Unicode code point UC may be a character in an identifier
   other than the first. */
bool
lex_uc_is_idn (ucs4_t uc)
{
  return (is_ascii_id1 (uc) || isdigit (uc) || uc == '.' || uc == '_'
          || (uc >= 0x80 && uc_is_property_id_continue (uc)));
}

/* Returns true if Unicode code point UC is a space that separates tokens. */
bool
lex_uc_is_space (ucs4_t uc)
{
  /* These are all of the Unicode characters in category Zs, Zl, or Zp.  */
  return (uc == ' ' || (uc <= 0x000d && uc >= 0x0009)
          || (uc >= 0x80
              && (uc == 0xa0 || uc == 0x85 || uc == 0x1680 || uc == 0x180e
                  || (uc >= 0x2000 && uc <= 0x200a)
                  || uc == 0x2028 || uc == 0x2029 || uc == 0x202f
                  || uc == 0x205f || uc == 0x3000)));
}


/* Returns the length of the longest prefix of STRING that forms
   a valid identifier.  Returns zero if STRING does not begin
   with a valid identifier.  */
size_t
lex_id_get_length (struct substring string)
{
  size_t length = 0;
  if (!ss_is_empty (string) && lex_is_id1 (ss_first (string)))
    {
      length = 1;
      while (length < ss_length (string)
             && lex_is_idn (ss_at (string, length)))
        length++;
    }
  return length;
}

/* Comparing identifiers. */

/* Returns true if TOKEN is a case-insensitive match for KEYWORD.

   Keywords match if one of the following is true: KEYWORD and
   TOKEN are identical, or TOKEN is at least 3 characters long
   and those characters are identical to KEYWORD.  (Letters that
   differ only in case are considered identical.)

   KEYWORD must be ASCII, but TOKEN may be ASCII or UTF-8. */
bool
lex_id_match (struct substring keyword, struct substring token)
{
  return lex_id_match_n (keyword, token, 3);
}

/* Returns true if TOKEN is a case-insensitive match for at least
   the first N characters of KEYWORD.

   KEYWORD must be ASCII, but TOKEN may be ASCII or UTF-8. */
bool
lex_id_match_n (struct substring keyword, struct substring token, size_t n)
{
  size_t token_len = ss_length (token);
  size_t keyword_len = ss_length (keyword);

  if (token_len >= n && token_len < keyword_len)
    return ss_equals_case (ss_head (keyword, token_len), token);
  else
    return ss_equals_case (keyword, token);
}

/* Table of keywords. */
struct keyword
  {
    int token;
    const struct substring identifier;
  };

static const struct keyword keywords[] =
  {
    { T_AND,  SS_LITERAL_INITIALIZER ("AND") },
    { T_OR,   SS_LITERAL_INITIALIZER ("OR") },
    { T_NOT,  SS_LITERAL_INITIALIZER ("NOT") },
    { T_EQ,   SS_LITERAL_INITIALIZER ("EQ") },
    { T_GE,   SS_LITERAL_INITIALIZER ("GE") },
    { T_GT,   SS_LITERAL_INITIALIZER ("GT") },
    { T_LE,   SS_LITERAL_INITIALIZER ("LE") },
    { T_LT,   SS_LITERAL_INITIALIZER ("LT") },
    { T_NE,   SS_LITERAL_INITIALIZER ("NE") },
    { T_ALL,  SS_LITERAL_INITIALIZER ("ALL") },
    { T_BY,   SS_LITERAL_INITIALIZER ("BY") },
    { T_TO,   SS_LITERAL_INITIALIZER ("TO") },
    { T_WITH, SS_LITERAL_INITIALIZER ("WITH") },
  };
static const size_t keyword_cnt = sizeof keywords / sizeof *keywords;

/* Returns true if TOKEN is representable as a keyword. */
bool
lex_is_keyword (int token)
{
  const struct keyword *kw;
  for (kw = keywords; kw < &keywords[keyword_cnt]; kw++)
    if (kw->token == token)
      return true;
  return false;
}

/* Returns the proper token type, either T_ID or a reserved
   keyword enum, for ID. */
int
lex_id_to_token (struct substring id)
{
  if (ss_length (id) >= 2 && ss_length (id) <= 4)
    {
      const struct keyword *kw;
      for (kw = keywords; kw < &keywords[keyword_cnt]; kw++)
        if (ss_equals_case (kw->identifier, id))
          return kw->token;
    }

  return T_ID;
}

/* Returns the name for the given keyword token type. */
const char *
lex_id_name (int token)
{
  const struct keyword *kw;

  for (kw = keywords; kw < &keywords[keyword_cnt]; kw++)
    if (kw->token == token)
      {
        /* A "struct substring" is not guaranteed to be
           null-terminated, as our caller expects, but in this
           case it always will be. */
        return ss_data (kw->identifier);
      }
  NOT_REACHED ();
}
