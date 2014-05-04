/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2008, 2010, 2011, 2014 Free Software Foundation, Inc.

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

#include "ui/syntax-gen.h"

#include <ctype.h>
#include <mbchar.h>

#include "data/data-in.h"
#include "data/data-out.h"
#include "data/format.h"
#include "data/value.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/str.h"
#include "libpspp/misc.h"

#include "gl/c-ctype.h"
#include "gl/ftoastr.h"

/* Appends to OUTPUT a pair of hex digits for each byte in IN. */
static void
syntax_gen_hex_digits (struct string *output, struct substring in)
{
  size_t i;
  for (i = 0; i < in.length; i++)
    {
      unsigned char c = in.string[i];
      ds_put_byte (output, "0123456789ABCDEF"[c >> 4]);
      ds_put_byte (output, "0123456789ABCDEF"[c & 0xf]);
    }
}

/* Returns true if IN contains any control characters, false
   otherwise */
static bool
has_control_chars (struct substring in)
{
  size_t i;

  for (i = 0; i < in.length; i++)
    if (iscntrl ((unsigned char) in.string[i]))
      return true;
  return false;
}

static bool
has_single_quote (struct substring str)
{
  return (SIZE_MAX != ss_find_byte (str, '\''));
}

static bool
has_double_quote (struct substring str)
{
  return (SIZE_MAX != ss_find_byte (str, '"'));
}

/* Appends to OUTPUT valid PSPP syntax for a quoted string that
   contains IN.

   IN must be encoded in UTF-8, and the quoted result will also
   be encoded in UTF-8.

   The string will be output as a regular quoted string unless it
   contains control characters, in which case it is output as a
   hex string. */
void
syntax_gen_string (struct string *output, struct substring in)
{
  if (has_control_chars (in))
    {
      ds_put_cstr (output, "X'");
      syntax_gen_hex_digits (output, in);
      ds_put_byte (output, '\'');
    }
  else
    {
      int quote;
      size_t i;

      /* This seemingly simple implementation is possible, because UTF-8
         guarantees that bytes corresponding to basic characters (such as
         '\'') cannot appear in a multi-byte character sequence except to
         represent that basic character.
      */
      assert (is_basic ('\''));

      quote = has_double_quote (in) && !has_single_quote (in) ? '\'' : '"';
      ds_put_byte (output, quote);
      for (i = 0; i < in.length; i++)
        {
          char c = in.string[i];
          if (c == quote)
            ds_put_byte (output, quote);
          ds_put_byte (output, c);
        }
      ds_put_byte (output, quote);
    }
}

/* Appends to OUTPUT a representation of NUMBER in PSPP syntax.
   The representation is precise, that is, when PSPP parses the
   representation, its value will be exactly NUMBER.  (This might
   not be the case on a C implementation where double has a
   different representation.)

   If NUMBER is the system-missing value, it is output as the
   identifier SYSMIS.  This may not be appropriate, because
   SYSMIS is not consistently parsed throughout PSPP syntax as
   the system-missing value.  But in such circumstances the
   system-missing value would not be meaningful anyhow, so the
   caller should refrain from supplying the system-missing value
   in such cases.

   A value of LOWEST or HIGHEST is not treated specially.

   If FORMAT is null, then the representation will be in numeric
   form, e.g. 123 or 1.23e10.

   If FORMAT is non-null, then it must point to a numeric format.
   If the format is one easier for a user to understand when
   expressed as a string than as a number (for example, a date
   format), and the string representation precisely represents
   NUMBER, then the string representation is written to OUTPUT.
   Otherwise, NUMBER is output as if FORMAT was a null
   pointer. */
void
syntax_gen_number (struct string *output,
                   double number, const struct fmt_spec *format)
{
  assert (format == NULL || fmt_is_numeric (format->type));
  if (format != NULL
      && (format->type
          & (FMT_CAT_DATE | FMT_CAT_TIME | FMT_CAT_DATE_COMPONENT)))
    {
      union value v_in, v_out;
      char *s, *error;
      bool ok;

      v_in.f = number;
      s = data_out (&v_in, "FIXME",  format);

      /* FIXME: UTF8 encoded strings will fail here */
      error = data_in (ss_cstr (s), C_ENCODING, format->type, &v_out, 0, NULL);
      ok = error == NULL;
      free (error);

      if (ok && v_out.f == number)
        {
          syntax_gen_string (output, ss_cstr (s));
	  free (s);
          return;
        }
      free (s);
    }

  if (number == SYSMIS)
    ds_put_cstr (output, "SYSMIS");
  else
    {
      char s[DBL_BUFSIZE_BOUND];

      c_dtoastr (s, sizeof s, 0, 0, number);
      ds_put_cstr (output, s);
    }
}

/* Appends to OUTPUT a representation of VALUE, which has the
   specified WIDTH.  If FORMAT is non-null, it influences the
   output format.  The representation is precise, that is, when
   PSPP parses the representation, its value will be exactly
   VALUE. */
void
syntax_gen_value (struct string *output, const union value *value, int width,
                  const struct fmt_spec *format)
{
  assert (format == NULL || fmt_var_width (format) == width);
  if (width == 0)
    syntax_gen_number (output, value->f, format);
  else
    {
      char *s = CHAR_CAST_BUG (char *, value_str (value, width));
      syntax_gen_string (output, ss_buffer (s, width));
    }
}

/* Appends <low> THRU <high> to OUTPUT.  If LOW is LOWEST, then
   it is formatted as the identifier LO; if HIGH is HIGHEST, then
   it is formatted as the identifier HI.  Otherwise, LOW and HIGH
   are formatted as with a call to syntax_gen_num with the specified
   FORMAT.

   This is the opposite of the function parse_num_range. */
void
syntax_gen_num_range (struct string *output, double low, double high,
                      const struct fmt_spec *format)
{
  if (low == LOWEST)
    ds_put_cstr (output, "LO");
  else
    syntax_gen_number (output, low, format);

  ds_put_cstr (output, " THRU ");

  if (high == HIGHEST)
    ds_put_cstr (output, "HI");
  else
    syntax_gen_number (output, high, format);
}

/* Same as syntax_gen_pspp, below, but takes a va_list. */
void
syntax_gen_pspp_valist (struct string *output, const char *format,
                        va_list args)
{
  for (;;)
    {
      char qualifier[16];
      int precision = -1;
      char directive;
      size_t copy = strcspn (format, "%");
      ds_put_substring (output, ss_buffer (format, copy));
      format += copy;

      if (*format == '\0')
        return;
      assert (*format == '%');
      format++;
      directive = *format++;
      if (directive == '.')
        {
          int x = 0;
          while (directive = *format++, c_isdigit (directive))
            {
              assert (x < 16);
              qualifier[x++] = directive;
            }
          qualifier[x++] = '\0';
          precision = atoi (qualifier);
        }
      switch (directive)
        {
        case 's':
          {
            const char *s = va_arg (args, char *);
            switch (*format++)
              {
              case 'q':
                syntax_gen_string (output, ss_cstr (s));
                break;
              case 's':
                ds_put_cstr (output, s);
                break;
              default:
                NOT_REACHED ();
              }
          }
          break;

        case 'd':
          {
            int i = va_arg (args, int);
            ds_put_format (output, "%d", i);
          }
          break;

        case 'f':
	case 'g':
          {
	    char conv[32];
            double d = va_arg (args, double);
            int x = 0;
	    conv[x++] = '%';
            conv[x] = '\0';
            if (precision != -1)
              {
                strcat (conv, ".");
                strcat (conv, qualifier);
                x += strlen (qualifier) + 1;
              }
	    conv[x++] = directive;
	    conv[x++] = '\0';
            
            ds_put_c_format (output, conv, d);
            break;
          }

        case '%':
          ds_put_byte (output, '%');
          break;

        default:
          NOT_REACHED ();
        }
    }
}

/* printf-like function specialized for outputting PSPP syntax.
   FORMAT is appended to OUTPUT.  The following substitutions are
   supported:

     %sq: The char * argument is formatted as a PSPP string, as
          if with a call to syntax_gen_string.

     %ss: The char * argument is appended literally.

     %d: Same as printf's %d.

     %f %g: Same as printf.

     %%: Literal %.

   (These substitutions were chosen to allow GCC to check for
   correct argument types.)

   This function is somewhat experimental.  If it proves useful,
   the allowed substitutions will almost certainly be
   expanded. */
void
syntax_gen_pspp (struct string *output, const char *format, ...)
{
  va_list args;
  va_start (args, format);
  syntax_gen_pspp_valist (output, format, args);
  va_end (args);
}
