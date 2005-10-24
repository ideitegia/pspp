/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2005 Free Software Foundation, Inc.
   Written by John Darrington <john@darrington.wattle.id.au>

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

/* 
   This file is concerned with the definition of the PSPP syntax, NOT the 
   action of scanning/parsing code .
*/

#include <config.h>
#include "lex-def.h"


#include <assert.h>
#include <string.h>


/* Table of keywords. */
const char *keywords[T_N_KEYWORDS + 1] = 
  {
    "AND", "OR", "NOT",
    "EQ", "GE", "GT", "LE", "LT", "NE",
    "ALL", "BY", "TO", "WITH",
    NULL,
  };



/* Comparing identifiers. */

/* Keywords match if one of the following is true: KW and TOK are
   identical (except for differences in case), or TOK is at least 3
   characters long and those characters are identical to KW.  KW_LEN
   is the length of KW, TOK_LEN is the length of TOK. */
int
lex_id_match_len (const char *kw, size_t kw_len,
		  const char *tok, size_t tok_len)
{
  size_t i = 0;

  assert (kw && tok);
  for (;;)
    {
      if (i == kw_len && i == tok_len)
	return 1;
      else if (i == tok_len)
	return i >= 3;
      else if (i == kw_len)
	return 0;
      else if (toupper ((unsigned char) kw[i])
	       != toupper ((unsigned char) tok[i]))
	return 0;

      i++;
    }
}

/* Same as lex_id_match_len() minus the need to pass in the lengths. */
int
lex_id_match (const char *kw, const char *tok)
{
  return lex_id_match_len (kw, strlen (kw), tok, strlen (tok));
}



/* Returns the proper token type, either T_ID or a reserved keyword
   enum, for ID[], which must contain LEN characters. */
int
lex_id_to_token (const char *id, size_t len)
{
  const char **kwp;

  if (len < 2 || len > 4)
    return T_ID;
  
  for (kwp = keywords; *kwp; kwp++)
    if (!strcasecmp (*kwp, id))
      return T_FIRST_KEYWORD + (kwp - keywords);

  return T_ID;
}

