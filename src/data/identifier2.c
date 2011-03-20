/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2005, 2009, 2010, 2011 Free Software Foundation, Inc.

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

/* This file implements parts of identifier.h that call the msg() function.
   This allows test programs that do not use those functions to avoid linking
   additional object files. */

#include <config.h>

#include "data/identifier.h"

#include <string.h>
#include <unistr.h>

#include "libpspp/cast.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"

#include "gl/c-ctype.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Returns true if UTF-8 string ID is an acceptable identifier in encoding
   DICT_ENCODING (UTF-8 if null), false otherwise.  If ISSUE_ERROR is true,
   issues an explanatory error message on failure. */
bool
id_is_valid (const char *id, const char *dict_encoding, bool issue_error)
{
  size_t dict_len;

  if (!id_is_plausible (id, issue_error))
    return false;

  if (dict_encoding != NULL)
    {
      /* XXX need to reject recoded strings that contain the fallback
         character. */
      dict_len = recode_string_len (dict_encoding, "UTF-8", id, -1);
    }
  else
    dict_len = strlen (id);

  if (dict_len > ID_MAX_LEN)
    {
      if (issue_error)
        msg (SE, _("Identifier `%s' exceeds %d-byte limit."),
             id, ID_MAX_LEN);
      return false;
    }

  return true;
}

/* Returns true if UTF-8 string ID is an plausible identifier, false
   otherwise.  If ISSUE_ERROR is true, issues an explanatory error message on
   failure.  */
bool
id_is_plausible (const char *id, bool issue_error)
{
  const uint8_t *bad_unit;
  const uint8_t *s;
  char ucname[16];
  int mblen;
  ucs4_t uc;

  /* ID cannot be the empty string. */
  if (id[0] == '\0')
    {
      if (issue_error)
        msg (SE, _("Identifier cannot be empty string."));
      return false;
    }

  /* ID cannot be a reserved word. */
  if (lex_id_to_token (ss_cstr (id)) != T_ID)
    {
      if (issue_error)
        msg (SE, _("`%s' may not be used as an identifier because it "
                   "is a reserved word."), id);
      return false;
    }

  bad_unit = u8_check (CHAR_CAST (const uint8_t *, id), strlen (id));
  if (bad_unit != NULL)
    {
      /* If this message ever appears, it probably indicates a PSPP bug since
         it shouldn't be possible to get invalid UTF-8 this far. */
      if (issue_error)
        msg (SE, _("`%s' may not be used as an identifier because it "
                   "contains ill-formed UTF-8 at byte offset %tu."),
             id, CHAR_CAST (const char *, bad_unit) - id);
      return false;
    }

  /* Check that it is a valid identifier. */
  mblen = u8_strmbtouc (&uc, CHAR_CAST (uint8_t *, id));
  if (!lex_uc_is_id1 (uc))
    {
      if (issue_error)
        msg (SE, _("Character %s (in `%s') may not appear "
                   "as the first character in a identifier."),
             uc_name (uc, ucname), id);
      return false;
    }

  for (s = CHAR_CAST (uint8_t *, id + mblen);
       (mblen = u8_strmbtouc (&uc, s)) != 0;
        s += mblen)
    if (!lex_uc_is_idn (uc))
      {
        if (issue_error)
          msg (SE, _("Character %s (in `%s') may not appear in an "
                     "identifier."),
               uc_name (uc, ucname), id);
        return false;
      }

  return true;
}
