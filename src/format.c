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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA. */

#include <config.h>
#include "format.h"
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include "error.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"

#define DEFFMT(LABEL, NAME, N_ARGS, IMIN_W, IMAX_W, OMIN_W, OMAX_W, CAT, \
	       OUTPUT, SPSS_FMT) \
	{NAME, N_ARGS, IMIN_W, IMAX_W, OMIN_W, OMAX_W, CAT, OUTPUT, SPSS_FMT},
struct fmt_desc formats[FMT_NUMBER_OF_FORMATS + 1] =
{
#include "format.def"
  {"",         -1, -1,  -1, -1,   -1, 0000, -1, -1},
};

/* Parses the alphabetic prefix of the current token as a format
   specifier name.  Returns the corresponding format specifier
   type if successful, or -1 on failure.  If ALLOW_XT is zero,
   then X and T format specifiers are not allowed.  If CP is
   nonzero, then *CP is set to the first non-alphabetic character
   in the current token on success or to a null pointer on
   failure. */
int
parse_format_specifier_name (const char **cp, int allow_xt)
{
  char *sp, *ep;
  int idx;

  sp = ep = ds_value (&tokstr);
  while (isalpha ((unsigned char) *ep))
    ep++;

  if (sp != ep) 
    {
      /* Find format. */
      for (idx = 0; idx < FMT_NUMBER_OF_FORMATS; idx++)
        if (strlen (formats[idx].name) == ep - sp
            && memcmp (formats[idx].name, sp, ep - sp))
          break;

      /* Check format. */
      if (idx < FMT_NUMBER_OF_FORMATS)
        {
          if (!allow_xt && (idx == FMT_T || idx == FMT_X)) 
            {
              msg (SE, _("X and T format specifiers not allowed here."));
              idx = -1; 
            }
        }
      else 
        {
          /* No match. */
          msg (SE, _("%.*s is not a valid data format."),
               (int) (ep - sp), ds_value (&tokstr));
          idx = -1; 
        }
    }
  else 
    {
      lex_error ("expecting data format");
      idx = -1;
    }
      
  if (cp != NULL)
    *cp = ep;

  return idx;
}

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

/* Checks whether SPEC is valid as an input format and returns
   nonzero if so.  Otherwise, emits an error message and returns
   zero. */
int
check_input_specifier (const struct fmt_spec *spec)
{
  struct fmt_desc *f;
  char *str;

  f = &formats[spec->type];
  str = fmt_to_string (spec);
  if (spec->type == FMT_X)
    return 1;
  if (f->cat & FCAT_OUTPUT_ONLY)
    {
      msg (SE, _("Format %s may not be used as an input format."), f->name);
      return 0;
    }
  if (spec->w < f->Imin_w || spec->w > f->Imax_w)
    {
      msg (SE, _("Input format %s specifies a bad width %d.  "
		 "Format %s requires a width between %d and %d."),
	   str, spec->w, f->name, f->Imin_w, f->Imax_w);
      return 0;
    }
  if ((f->cat & FCAT_EVEN_WIDTH) && spec->w % 2)
    {
      msg (SE, _("Input format %s specifies an odd width %d, but "
		 "format %s requires an even width between %d and "
		 "%d."), str, spec->w, f->name, f->Imin_w, f->Imax_w);
      return 0;
    }
  if (f->n_args > 1 && (spec->d < 0 || spec->d > 16))
    {
      msg (SE, _("Input format %s specifies a bad number of "
		 "implied decimal places %d.  Input format %s allows "
		 "up to 16 implied decimal places."), str, spec->d, f->name);
      return 0;
    }
  return 1;
}

/* Checks whether SPEC is valid as an output format and returns
   nonzero if so.  Otherwise, emits an error message and returns
   zero. */
int
check_output_specifier (const struct fmt_spec *spec)
{
  struct fmt_desc *f;
  char *str;

  f = &formats[spec->type];
  str = fmt_to_string (spec);
  if (spec->type == FMT_X)
    return 1;
  if (spec->w < f->Omin_w || spec->w > f->Omax_w)
    {
      msg (SE, _("Output format %s specifies a bad width %d.  "
		 "Format %s requires a width between %d and %d."),
	   str, spec->w, f->name, f->Omin_w, f->Omax_w);
      return 0;
    }
  if (spec->d > 1
      && (spec->type == FMT_F || spec->type == FMT_COMMA
	  || spec->type == FMT_DOLLAR)
      && spec->w < f->Omin_w + 1 + spec->d)
    {
      msg (SE, _("Output format %s requires minimum width %d to allow "
		 "%d decimal places.  Try %s%d.%d instead of %s."),
	   f->name, f->Omin_w + 1 + spec->d, spec->d, f->name,
	   f->Omin_w + 1 + spec->d, spec->d, str);
      return 0;
    }
  if ((f->cat & FCAT_EVEN_WIDTH) && spec->w % 2)
    {
      msg (SE, _("Output format %s specifies an odd width %d, but "
		 "output format %s requires an even width between %d and "
		 "%d."), str, spec->w, f->name, f->Omin_w, f->Omax_w);
      return 0;
    }
  if (f->n_args > 1 && (spec->d < 0 || spec->d > 16))
    {
      msg (SE, _("Output format %s specifies a bad number of "
		 "implied decimal places %d.  Output format %s allows "
		 "a number of implied decimal places between 1 "
		 "and 16."), str, spec->d, f->name);
      return 0;
    }
  return 1;
}

/* If a string variable has width W, you can't display it with a
   format specifier with a required width MIN_LEN>W. */
int
check_string_specifier (const struct fmt_spec *f, int min_len)
{
  if ((f->type == FMT_A && min_len > f->w)
      || (f->type == FMT_AHEX && min_len * 2 > f->w))
    {
      msg (SE, _("Can't display a string variable of width %d with "
		 "format specifier %s."), min_len, fmt_to_string (f));
      return 0;
    }
  return 1;
}

/* Converts input format specifier INPUT into output format
   specifier OUTPUT. */
void
convert_fmt_ItoO (const struct fmt_spec *input, struct fmt_spec *output)
{
  output->type = formats[input->type].output;
  output->w = input->w;
  if (output->w > formats[output->type].Omax_w)
    output->w = formats[output->type].Omax_w;
  output->d = input->d;

  switch (input->type)
    {
    case FMT_F:
    case FMT_N:
      if (output->d > 1 && output->w < 2 + output->d)
	output->w = 2 + output->d;
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
}

/* Parses a format specifier from the token stream and returns
   nonzero only if successful.  Emits an error message on
   failure.  Allows X and T format specifiers only if ALLOW_XT is
   nonzero.  The caller should call check_input_specifier() or
   check_output_specifier() on the parsed format as
   necessary.  */
int
parse_format_specifier (struct fmt_spec *input, int allow_xt)
{
  struct fmt_spec spec;
  struct fmt_desc *f;
  const char *cp;
  char *cp2;
  int type, w, d;

  if (token != T_ID)
    {
      msg (SE, _("Format specifier expected."));
      return 0;
    }
  type = parse_format_specifier_name (&cp, allow_xt);
  if (type == -1)
    return 0;
  f = &formats[type];

  w = strtol (cp, &cp2, 10);
  if (cp2 == cp && type != FMT_X)
    {
      msg (SE, _("Data format %s does not specify a width."),
	   ds_value (&tokstr));
      return 0;
    }

  cp = cp2;
  if (f->n_args > 1 && *cp == '.')
    {
      cp++;
      d = strtol (cp, &cp2, 10);
      cp = cp2;
    }
  else
    d = 0;

  if (*cp)
    {
      msg (SE, _("Data format %s is not valid."), ds_value (&tokstr));
      return 0;
    }
  lex_get ();

  spec.type = type;
  spec.w = w;
  spec.d = d;
  *input = spec;

  return 1;
}

/* Returns the width corresponding to the format specifier.  The
   return value is the value of the `width' member of a `struct
   variable' for such an input format. */
int
get_format_var_width (const struct fmt_spec *spec) 
{
  if (spec->type == FMT_AHEX)
    return spec->w * 2;
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
