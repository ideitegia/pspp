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

#include <config.h>
#include "format.h"
#include <ctype.h>
#include <libpspp/message.h>
#include <stdlib.h>
#include <libpspp/compiler.h>
#include <libpspp/misc.h>
#include "identifier.h"
#include <libpspp/str.h>
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

#define DEFFMT(LABEL, NAME, N_ARGS, IMIN_W, IMAX_W, OMIN_W, OMAX_W, CAT, \
	       OUTPUT, SPSS_FMT) \
	{NAME, N_ARGS, IMIN_W, IMAX_W, OMIN_W, OMAX_W, CAT, OUTPUT, SPSS_FMT},
struct fmt_desc formats[FMT_NUMBER_OF_FORMATS + 1] =
{
#include "format.def"
  {"",         -1, -1,  -1, -1,   -1, 0000, -1, -1},
};

/* Common formats. */
const struct fmt_spec f8_2 = {FMT_F, 8, 2};

/* Converts F to its string representation (for instance, "F8.2") and
   returns a pointer to a static buffer containing that string. */
char *
fmt_to_string (const struct fmt_spec *f)
{
  static char buf[32];

  if (formats[f->type].n_args >= 2)
    sprintf (buf, "%s%d.%d", formats[f->type].name, f->w, f->d);
  else
    sprintf (buf, "%s%d", formats[f->type].name, f->w);
  return buf;
}

/* Does checks in common betwen check_input_specifier() and
   check_output_specifier() and returns true if so.  Otherwise,
   emits an error message (if EMIT_ERROR is nonzero) and returns
   false. */
static bool
check_common_specifier (const struct fmt_spec *spec, bool emit_error)
{
  struct fmt_desc *f ; 
  char *str;

  if ( spec->type > FMT_NUMBER_OF_FORMATS ) 
    {
      if (emit_error)
        msg (SE, _("Format specifies a bad type (%d)"), spec->type);
      
      return false;
    }

  f = &formats[spec->type];
  str = fmt_to_string (spec);

  if ((f->cat & FCAT_EVEN_WIDTH) && spec->w % 2)
    {
      if (emit_error)
        msg (SE, _("Format %s specifies an odd width %d, but "
                   "an even width is required."),
             str, spec->w);
      return false;
    }
  if (f->n_args > 1 && (spec->d < 0 || spec->d > 16))
    {
      if (emit_error)
        msg (SE, _("Format %s specifies a bad number of "
                   "implied decimal places %d.  Input format %s allows "
                   "up to 16 implied decimal places."), str, spec->d, f->name);
      return false;
    }
  return true;
}

/* Checks whether SPEC is valid as an input format and returns
   nonzero if so.  Otherwise, emits an error message (if
   EMIT_ERROR is nonzero) and returns zero. */
int
check_input_specifier (const struct fmt_spec *spec, int emit_error)
{
  struct fmt_desc *f ;
  char *str ;

  if (!check_common_specifier (spec, emit_error))
    return false;

  f = &formats[spec->type];
  str = fmt_to_string (spec);


  if (spec->type == FMT_X)
    return 1;
  if (f->cat & FCAT_OUTPUT_ONLY)
    {
      if (emit_error)
        msg (SE, _("Format %s may not be used for input."), f->name);
      return 0;
    }
  if (spec->w < f->Imin_w || spec->w > f->Imax_w)
    {
      if (emit_error)
        msg (SE, _("Input format %s specifies a bad width %d.  "
                   "Format %s requires a width between %d and %d."),
             str, spec->w, f->name, f->Imin_w, f->Imax_w);
      return 0;
    }
  if ((spec->type == FMT_F || spec->type == FMT_COMMA
	  || spec->type == FMT_DOLLAR)
      && spec->d > spec->w)
    {
      if (emit_error)
        msg (SE, _("Input format %s is invalid because it specifies more "
                   "decimal places than the field width."), str);
      return 0;
    }
  return 1;
}

/* Checks whether SPEC is valid as an output format and returns
   nonzero if so.  Otherwise, emits an error message (if
   EMIT_ERROR is nonzero) and returns zero. */
int
check_output_specifier (const struct fmt_spec *spec, int emit_error)
{
  struct fmt_desc *f;
  char *str ; 

  if (!check_common_specifier (spec, emit_error))
    return false;

  f = &formats[spec->type];
  str = fmt_to_string (spec);

  if (spec->type == FMT_X)
    return 1;
  if (spec->w < f->Omin_w || spec->w > f->Omax_w)
    {
      if (emit_error)
        msg (SE, _("Output format %s specifies a bad width %d.  "
                   "Format %s requires a width between %d and %d."),
             str, spec->w, f->name, f->Omin_w, f->Omax_w);
      return 0;
    }
  if ((spec->type == FMT_F || spec->type == FMT_COMMA
	  || spec->type == FMT_DOLLAR)
      && spec->d >= spec->w)
    {
      if (emit_error)
        msg (SE, _("Output format %s is invalid because it specifies as "
                   "many decimal places as the field width, which "
                   "fails to allow space for a decimal point.  "
                   "Try %s%d.%d instead."),
             str, f->name, spec->d + 1, spec->d);
      return 0;
    }
  return 1;
}

/* Checks that FORMAT is appropriate for a variable of the given
   TYPE and returns true if so.  Otherwise returns false and (if
   EMIT_ERROR is true) emits an error message. */
bool
check_specifier_type (const struct fmt_spec *format,
                      int type, bool emit_error) 
{
  const struct fmt_desc *f = &formats[format->type];
  assert (type == NUMERIC || type == ALPHA);
  if ((type == ALPHA) != ((f->cat & FCAT_STRING) != 0))
    {
      if (emit_error)
        msg (SE, _("%s variables are not compatible with %s format %s."),
             type == ALPHA ? _("String") : _("Numeric"),
             type == ALPHA ? _("numeric") : _("string"),
             fmt_to_string (format));
      return false;
    }
  return true;
}
  
/* Checks that FORMAT is appropriate for a variable of the given
   WIDTH and returns true if so.  Otherwise returns false and (if
   EMIT_ERROR is true) emits an error message. */
bool
check_specifier_width (const struct fmt_spec *format,
                       int width, bool emit_error) 
{
  if (!check_specifier_type (format, width != 0 ? ALPHA : NUMERIC, emit_error))
    return false;
  if (get_format_var_width (format) != width)
    {
      if (emit_error)
        msg (SE, _("String variable with width %d not compatible with "
                   "format %s."),
             width, fmt_to_string (format));
      return false;
    }
  return true;
}

/* Converts input format specifier INPUT into output format
   specifier OUTPUT. */
void
convert_fmt_ItoO (const struct fmt_spec *input, struct fmt_spec *output)
{
  assert (check_input_specifier (input, 0));

  output->type = formats[input->type].output;
  output->w = input->w;
  if (output->w > formats[output->type].Omax_w)
    output->w = formats[output->type].Omax_w;
  output->d = input->d;

  switch (input->type)
    {
    case FMT_F:
    case FMT_N:
      if (output->d > 0)
	output->w++;
      break;
    case FMT_E:
      output->w = max (max (input->w, input->d+7), 10);
      output->d = max (input->d, 3);
      break;
    case FMT_COMMA:
    case FMT_DOT:
      /* nothing is necessary */
      break;
    case FMT_DOLLAR:
    case FMT_PCT:
      if (output->w < 2)
	output->w = 2;
      break;
    case FMT_PIBHEX:
      {
	static const int map[] = {4, 6, 9, 11, 14, 16, 18, 21};
	assert (input->w % 2 == 0 && input->w >= 2 && input->w <= 16);
	output->w = map[input->w / 2 - 1];
	break;
      }
    case FMT_RBHEX:
      output->w = 8, output->d = 2;	/* FIXME */
      break;
    case FMT_IB:
    case FMT_PIB:
    case FMT_P:
    case FMT_PK:
    case FMT_RB:
      if (input->d < 1)
	output->w = 8, output->d = 2;
      else
	output->w = 9 + input->d;
      break;
    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      assert (0);
    case FMT_Z:
    case FMT_A:
      /* nothing is necessary */
      break;
    case FMT_AHEX:
      output->w = input->w / 2;
      break;
    case FMT_DATE:
    case FMT_EDATE:
    case FMT_SDATE:
    case FMT_ADATE:
    case FMT_JDATE:
      /* nothing is necessary */
      break;
    case FMT_QYR:
      if (output->w < 6)
	output->w = 6;
      break;
    case FMT_MOYR:
      /* nothing is necessary */
      break;
    case FMT_WKYR:
      if (output->w < 8)
	output->w = 8;
      break;
    case FMT_TIME:
    case FMT_DTIME:
    case FMT_DATETIME:
    case FMT_WKDAY:
    case FMT_MONTH:
      /* nothing is necessary */
      break;
    default:
      assert (0);
    }

  assert (check_output_specifier (output, 0));
}

/* Returns the width corresponding to the format specifier.  The
   return value is the value of the `width' member of a `struct
   variable' for such an input format. */
int
get_format_var_width (const struct fmt_spec *spec) 
{
  if (spec->type == FMT_AHEX)
    return spec->w / 2;
  else if (spec->type == FMT_A)
    return spec->w;
  else
    return 0;
}

/* Returns the PSPP format corresponding to the given SPSS
   format. */
int
translate_fmt (int spss) 
{
  int type;
  
  for (type = 0; type < FMT_NUMBER_OF_FORMATS; type++)
    if (formats[type].spss == spss)
      return type;
  return -1;
}

/* Returns an input format specification with type TYPE, width W,
   and D decimals. */
struct fmt_spec
make_input_format (int type, int w, int d) 
{
  struct fmt_spec f;
  f.type = type;
  f.w = w;
  f.d = d;
  assert (check_input_specifier (&f, 0));
  return f;
}

/* Returns an output format specification with type TYPE, width
   W, and D decimals. */
struct fmt_spec
make_output_format (int type, int w, int d)
{
  struct fmt_spec f;
  f.type = type;
  f.w = w;
  f.d = d;
  assert (check_output_specifier (&f, 0));
  return f;
}


bool 
measure_is_valid(enum measure m)
{
  if ( m <= 0 ) return false;
  if ( m >= n_MEASURES) return false;
  return true;
}

bool 
alignment_is_valid(enum alignment a)
{
  if ( a >= n_ALIGN) return false;
  return true;
}
