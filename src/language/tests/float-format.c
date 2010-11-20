/* PSPP - a program for statistical analysis.
   Copyright (C) 2006, 2010 Free Software Foundation, Inc.

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

#include <libpspp/float-format.h>

#include "gettext.h"
#include <inttypes.h>

#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#define _(msgid) gettext (msgid)

/* Maximum supported size of a floating-point number, in bytes. */
#define FP_MAX_SIZE 32

/* A floating-point number tagged with its representation. */
struct fp
  {
    enum float_format format;           /* Format. */
    uint8_t data[FP_MAX_SIZE];          /* Representation. */
  };

/* Associates a format name with its identifier. */
struct assoc
  {
    char name[4];
    enum float_format format;
  };

/* List of floating-point formats. */
static const struct assoc fp_formats[] =
  {
    {"ISL", FLOAT_IEEE_SINGLE_LE},
    {"ISB", FLOAT_IEEE_SINGLE_BE},
    {"IDL", FLOAT_IEEE_DOUBLE_LE},
    {"IDB", FLOAT_IEEE_DOUBLE_BE},
    {"VF", FLOAT_VAX_F},
    {"VD", FLOAT_VAX_D},
    {"VG", FLOAT_VAX_G},
    {"ZS", FLOAT_Z_SHORT},
    {"ZL", FLOAT_Z_LONG},
    {"X", FLOAT_HEX},
    {"FP", FLOAT_FP},
  };
static const size_t format_cnt = sizeof fp_formats / sizeof *fp_formats;

/* Parses a floating-point format name into *FORMAT,
   and returns success. */
static bool
parse_float_format (struct lexer *lexer, enum float_format *format)
{
  size_t i;

  for (i = 0; i < format_cnt; i++)
    if (lex_match_id (lexer, fp_formats[i].name))
      {
        *format = fp_formats[i].format;
        return true;
      }
  lex_error (lexer, "expecting floating-point format identifier");
  return false;
}

/* Returns the name for the given FORMAT. */
static const char *
get_float_format_name (enum float_format format)
{
  size_t i;

  for (i = 0; i < format_cnt; i++)
    if (fp_formats[i].format == format)
      return fp_formats[i].name;

  NOT_REACHED ();
}

/* Parses a number in the form FORMAT(STRING), where FORMAT is
   the name of the format and STRING gives the number's
   representation.  Also supports ordinary floating-point numbers
   written in decimal notation.  Returns success. */
static bool
parse_fp (struct lexer *lexer, struct fp *fp)
{
  if (lex_is_number (lexer))
    {
      double number = lex_number (lexer);
      fp->format = FLOAT_NATIVE_DOUBLE;
      memcpy (fp->data, &number, sizeof number);
      lex_get (lexer);
    }
  else if (lex_token (lexer) == T_ID)
    {
      size_t length;

      if (!parse_float_format (lexer, &fp->format)
          || !lex_force_match (lexer, T_LPAREN)
          || !lex_force_string (lexer))
        return false;

      length = ss_length (lex_tokss (lexer));
      if (fp->format != FLOAT_HEX)
        {
          if (length != float_get_size (fp->format))
            {
              msg (SE, _("%zu-byte string needed but %zu-byte string "
                         "supplied."),
                   float_get_size (fp->format), length);
              return false;
            }
          assert (length <= sizeof fp->data);
          memcpy (fp->data, ss_data (lex_tokss (lexer)), length);
        }
      else
        {
          if (length >= sizeof fp->data)
            {
              msg (SE, _("Hexadecimal floating constant too long."));
              return false;
            }
          strncpy (CHAR_CAST_BUG (char *,fp->data), lex_tokcstr (lexer), sizeof fp->data);
        }

      lex_get (lexer);
      if (!lex_force_match (lexer, T_RPAREN))
        return false;
    }
  else
    {
      lex_error (lexer, NULL);
      return false;
    }
  return true;
}

/* Renders SRC, which contains SRC_SIZE bytes of a floating-point
   number in the given FORMAT, as relatively human-readable
   null-terminated string in the DST_SIZE bytes in DST.  DST_SIZE
   must be be large enough to hold the output. */
static void
make_printable (enum float_format format, const void *src_, size_t src_size,
                char *dst, size_t dst_size)
{
  assert (dst_size >= 2 * src_size + 1);
  if (format != FLOAT_HEX)
    {
      const uint8_t *src = src_;
      while (src_size-- > 0)
        {
          sprintf (dst, "%02x", *src++);
          dst += 2;
        }
      *dst = '\0';
    }
  else
    strncpy (dst, src_, src_size + 1);
}

/* Checks that RESULT is identical to TO.
   If so, returns false.
   If not, issues a helpful error message that includes the given
   CONVERSION_TYPE and the value that was converted FROM, and
   returns true. */
static bool
mismatch (const struct fp *from, const struct fp *to, char *result,
          const char *conversion_type)
{
  size_t to_size = float_get_size (to->format);
  if (!memcmp (to->data, result, to_size))
    return false;
  else
    {
      size_t from_size = float_get_size (from->format);
      char original[FP_MAX_SIZE * 2 + 1];
      char expected[FP_MAX_SIZE * 2 + 1];
      char actual[FP_MAX_SIZE * 2 + 1];
      make_printable (from->format, from->data, from_size, original,
                      sizeof original);
      make_printable (to->format, to->data, to_size, expected,
                      sizeof expected);
      make_printable (to->format, result, to_size, actual, sizeof actual);
      msg (SE,
           _("%s conversion of %s from %s to %s should have produced %s "
             "but actually produced %s."),
           conversion_type,
           original, get_float_format_name (from->format),
           get_float_format_name (to->format), expected,
           actual);
      return true;
    }
}

/* Checks that converting FROM into the format of TO yields
   exactly the data in TO. */
static bool
verify_conversion (const struct fp *from, const struct fp *to)
{
  char tmp1[FP_MAX_SIZE], tmp2[FP_MAX_SIZE];

  /* First try converting directly. */
  float_convert (from->format, from->data, to->format, tmp1);
  if (mismatch (from, to, tmp1, "Direct"))
    return false;

  /* Then convert via FLOAT_FP to prevent short-circuiting that
     float_convert() does for some conversions (e.g. little<->big
     endian for IEEE formats). */
  float_convert (from->format, from->data, FLOAT_FP, tmp1);
  float_convert (FLOAT_FP, tmp1, to->format, tmp2);
  if (mismatch (from, to, tmp2, "Indirect"))
    return false;

  return true;
}

/* Executes the DEBUG FLOAT FORMAT command. */
int
cmd_debug_float_format (struct lexer *lexer, struct dataset *ds UNUSED)
{
  struct fp fp[16];
  size_t fp_cnt = 0;
  bool bijective = false;
  bool ok;

  for (;;)
    {
      if (fp_cnt >= sizeof fp / sizeof *fp)
        {
          msg (SE, _("Too many values in single command."));
          return CMD_FAILURE;
        }
      if (!parse_fp (lexer, &fp[fp_cnt++]))
        return CMD_FAILURE;

      if (lex_token (lexer) == T_ENDCMD && fp_cnt > 1)
        break;
      else if (!lex_force_match (lexer, T_EQUALS))
        return CMD_FAILURE;

      if (fp_cnt == 1)
        {
          if (lex_match (lexer, T_EQUALS))
            bijective = true;
          else if (lex_match (lexer, T_GT))
            bijective = false;
          else
            {
              lex_error (lexer, NULL);
              return CMD_FAILURE;
            }
        }
      else
        {
          if ((bijective && !lex_force_match (lexer, T_EQUALS))
              || (!bijective && !lex_force_match (lexer, T_GT)))
            return CMD_FAILURE;
        }
    }

  ok = true;
  if (bijective)
    {
      size_t i, j;

      for (i = 0; i < fp_cnt; i++)
        for (j = 0; j < fp_cnt; j++)
          if (!verify_conversion (&fp[i], &fp[j]))
            ok = false;
    }
  else
    {
      size_t i;

      for (i = 1; i < fp_cnt; i++)
        if (!verify_conversion (&fp[i - 1], &fp[i]))
          ok = false;
    }

  return ok ? CMD_SUCCESS : CMD_FAILURE;
}
