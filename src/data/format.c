/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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
#include <stdlib.h>

#include <data/identifier.h>
#include <data/settings.h>
#include <data/variable.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "minmax.h"
#include "xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static bool is_fmt_type (enum fmt_type);

static int min_width (enum fmt_type, bool for_input);
static int max_width (enum fmt_type);
static bool valid_width (enum fmt_type, int width, bool for_input);
static int max_decimals (enum fmt_type, int width, bool for_input);

static int max_digits_for_bytes (int bytes);

/* Initialize the format module. */
void
fmt_init (void)
{
  static bool inited = false;
  if (!inited)
    {
      inited = true;
      fmt_set_decimal ('.');
    }
}

/* Deinitialize the format module. */
void
fmt_done (void)
{
}

/* Returns an input format specification with type TYPE, width W,
   and D decimals. */
struct fmt_spec
fmt_for_input (enum fmt_type type, int w, int d)
{
  struct fmt_spec f;
  f.type = type;
  f.w = w;
  f.d = d;
  assert (fmt_check_input (&f));
  return f;
}

/* Returns an output format specification with type TYPE, width
   W, and D decimals. */
struct fmt_spec
fmt_for_output (enum fmt_type type, int w, int d)
{
  struct fmt_spec f;
  f.type = type;
  f.w = w;
  f.d = d;
  assert (fmt_check_output (&f));
  return f;
}

/* Returns the output format specifier corresponding to input
   format specifier INPUT. */
struct fmt_spec
fmt_for_output_from_input (const struct fmt_spec *input)
{
  struct fmt_spec output;

  assert (fmt_check_input (input));

  output.type = fmt_input_to_output (input->type);
  output.w = input->w;
  if (output.w > fmt_max_output_width (output.type))
    output.w = fmt_max_output_width (output.type);
  else if (output.w < fmt_min_output_width (output.type))
    output.w = fmt_min_output_width (output.type);
  output.d = input->d;

  switch (input->type)
    {
    case FMT_Z:
      output.w++;
      if (output.d > 0)
	output.w++;
      break;

    case FMT_F:
    case FMT_COMMA:
    case FMT_DOT:
    case FMT_DOLLAR:
    case FMT_PCT:
      {
        const struct fmt_number_style *style = fmt_get_style (input->type);
        output.w += fmt_affix_width (style);
        if (style->grouping != 0 && input->w - input->d >= 3)
          output.w += (input->w - input->d - 1) / 3;
        if (output.d > 0)
          output.w++;
      }
      break;

    case FMT_N:
      if (output.d > 0)
        output.w++;
      break;

    case FMT_E:
      output.d = MAX (input->d, 3);
      output.w = MAX (input->w, output.d + 7);
      break;

    case FMT_PIBHEX:
      output.w = max_digits_for_bytes (input->w / 2) + 1;
      break;

    case FMT_RB:
    case FMT_RBHEX:
      output.w = 8;
      output.d = 2;
      break;

    case FMT_P:
    case FMT_PK:
      output.w = 2 * input->w + (input->d > 0);
      break;

    case FMT_IB:
    case FMT_PIB:
      output.w = max_digits_for_bytes (input->w) + 1;
      if (output.d > 0)
        output.w++;
      break;

    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      NOT_REACHED ();

    case FMT_A:
      break;

    case FMT_AHEX:
      output.w = input->w / 2;
      break;

    case FMT_DATE:
    case FMT_EDATE:
    case FMT_SDATE:
    case FMT_ADATE:
    case FMT_JDATE:
    case FMT_QYR:
    case FMT_MOYR:
    case FMT_WKYR:
    case FMT_TIME:
    case FMT_DTIME:
    case FMT_DATETIME:
    case FMT_WKDAY:
    case FMT_MONTH:
      break;

    default:
      NOT_REACHED ();
    }

  if (output.w > fmt_max_output_width (output.type))
    output.w = fmt_max_output_width (output.type);

  assert (fmt_check_output (&output));
  return output;
}

/* Checks whether SPEC is valid as an input format (if FOR_INPUT)
   or an output format (otherwise) and returns nonzero if so.
   Otherwise, emits an error message and returns zero. */
bool
fmt_check (const struct fmt_spec *spec, bool for_input)
{
  const char *io_fmt = for_input ? _("Input format") : _("Output format");
  char str[FMT_STRING_LEN_MAX + 1];
  int min_w, max_w, max_d;

  assert (is_fmt_type (spec->type));
  fmt_to_string (spec, str);

  if (for_input && !fmt_usable_for_input (spec->type))
    {
      msg (SE, _("Format %s may not be used for input."), str);
      return false;
    }

  if (spec->w % fmt_step_width (spec->type))
    {
      assert (fmt_step_width (spec->type) == 2);
      msg (SE, _("%s specifies width %d, but %s requires an even width."),
           str, spec->w, fmt_name (spec->type));
      return false;
    }

  min_w = min_width (spec->type, for_input);
  max_w = max_width (spec->type);
  if (spec->w < min_w || spec->w > max_w)
    {
      msg (SE, _("%s %s specifies width %d, but "
                 "%s requires a width between %d and %d."),
           io_fmt, str, spec->w, fmt_name (spec->type), min_w, max_w);
      return false;
    }

  max_d = max_decimals (spec->type, spec->w, for_input);
  if (!fmt_takes_decimals (spec->type) && spec->d != 0)
    {
      msg (SE, ngettext ("%s %s specifies %d decimal place, but "
                         "%s does not allow any decimals.",
                         "%s %s specifies %d decimal places, but "
                         "%s does not allow any decimals.",
                         spec->d),
           io_fmt, str, spec->d, fmt_name (spec->type));
      return false;
    }
  else if (spec->d > max_d)
    {
      if (max_d > 0)
        msg (SE, ngettext ("%s %s specifies %d decimal place, but "
                           "the given width allows at most %d decimals.",
                           "%s %s specifies %d decimal places, but "
                           "the given width allows at most %d decimals.",
                           spec->d),
             io_fmt, str, spec->d, max_d);
      else
        msg (SE, ngettext ("%s %s specifies %d decimal place, but "
                           "the given width does not allow for any decimals.",
                           "%s %s specifies %d decimal places, but "
                           "the given width does not allow for any decimals.",
                           spec->d),
             io_fmt, str, spec->d);
      return false;
    }

  return true;
}

/* Checks whether SPEC is valid as an input format and returns
   nonzero if so.  Otherwise, emits an error message and returns
   zero. */
bool
fmt_check_input (const struct fmt_spec *spec)
{
  return fmt_check (spec, true);
}

/* Checks whether SPEC is valid as an output format and returns
   true if so.  Otherwise, emits an error message and returns false. */
bool
fmt_check_output (const struct fmt_spec *spec)
{
  return fmt_check (spec, false);
}

/* Checks that FORMAT is appropriate for a variable of the given
   TYPE and returns true if so.  Otherwise returns false and
   emits an error message. */
bool
fmt_check_type_compat (const struct fmt_spec *format, int var_type)
{
  assert (var_type == NUMERIC || var_type == ALPHA);
  if ((var_type == ALPHA) != (fmt_is_string (format->type) != 0))
    {
      char str[FMT_STRING_LEN_MAX + 1];
      msg (SE, _("%s variables are not compatible with %s format %s."),
           var_type == ALPHA ? _("String") : _("Numeric"),
           var_type == ALPHA ? _("numeric") : _("string"),
           fmt_to_string (format, str));
      return false;
    }
  return true;
}

/* Checks that FORMAT is appropriate for a variable of the given
   WIDTH and returns true if so.  Otherwise returns false and
   emits an error message. */
bool
fmt_check_width_compat (const struct fmt_spec *format, int width)
{
  if (!fmt_check_type_compat (format, width != 0 ? ALPHA : NUMERIC))
    return false;
  if (fmt_var_width (format) != width)
    {
      char str[FMT_STRING_LEN_MAX + 1];
      msg (SE, _("String variable with width %d is not compatible with "
                 "format %s."),
           width, fmt_to_string (format, str));
      return false;
    }
  return true;
}

/* Returns the width corresponding to the format specifier.  The
   return value is the value of the `width' member of a `struct
   variable' for such an input format. */
int
fmt_var_width (const struct fmt_spec *spec)
{
  return (spec->type == FMT_AHEX ? spec->w / 2
          : spec->type == FMT_A ? spec->w
          : 0);
}

/* Converts F to its string representation (for instance, "F8.2")
   in BUFFER.  Returns BUFFER.

   If F has decimals, they are included in the output string,
   even if F's format type does not allow decimals, to allow
   accurately presenting incorrect formats to the user. */
char *
fmt_to_string (const struct fmt_spec *f, char buffer[FMT_STRING_LEN_MAX + 1])
{
  if (fmt_takes_decimals (f->type) || f->d > 0)
    snprintf (buffer, FMT_STRING_LEN_MAX + 1,
              "%s%d.%d", fmt_name (f->type), f->w, f->d);
  else
    snprintf (buffer, FMT_STRING_LEN_MAX + 1,
              "%s%d", fmt_name (f->type), f->w);
  return buffer;
}

/* Describes a display format. */
struct fmt_desc
  {
    char name[9];
    int min_input_width, min_output_width;
    int io;
    enum fmt_category category;
  };

static const struct fmt_desc *get_fmt_desc (enum fmt_type type);

/* Returns the name of the given format TYPE. */
const char *
fmt_name (enum fmt_type type)
{
  return get_fmt_desc (type)->name;
}

/* Tries to parse NAME as a format type.
   If successful, stores the type in *TYPE and returns true.
   On failure, returns false. */
bool
fmt_from_name (const char *name, enum fmt_type *type)
{
  int i;

  for (i = 0; i < FMT_NUMBER_OF_FORMATS; i++)
    if (!strcasecmp (name, get_fmt_desc (i)->name))
      {
        *type = i;
        return true;
      }
  return false;
}

/* Returns true if TYPE accepts decimal places,
   false otherwise. */
bool
fmt_takes_decimals (enum fmt_type type)
{
  return fmt_max_output_decimals (type, fmt_max_output_width (type)) > 0;
}

/* Returns the minimum acceptable width for an input field
   formatted with the given TYPE. */
int
fmt_min_input_width (enum fmt_type type)
{
  return get_fmt_desc (type)->min_input_width;
}

/* Returns the maximum acceptable width for an input field
   formatted with the given TYPE. */
int
fmt_max_input_width (enum fmt_type type)
{
  return max_width (type);
}

/* Returns the maximum number of decimal places allowed in an
   input field of the given TYPE and WIDTH. */
int
fmt_max_input_decimals (enum fmt_type type, int width)
{
  assert (valid_width (type, width, true));
  return max_decimals (type, width, true);
}

/* Returns the minimum acceptable width for an output field
   formatted with the given TYPE. */
int
fmt_min_output_width (enum fmt_type type)
{
  return get_fmt_desc (type)->min_output_width;
}

/* Returns the maximum acceptable width for an output field
   formatted with the given TYPE. */
int
fmt_max_output_width (enum fmt_type type)
{
  return max_width (type);
}

/* Returns the maximum number of decimal places allowed in an
   output field of the given TYPE and WIDTH. */
int
fmt_max_output_decimals (enum fmt_type type, int width)
{
  assert (valid_width (type, width, false));
  return max_decimals (type, width, false);
}

/* Returns the width step for a field formatted with the given
   TYPE.  Field width must be a multiple of the width step. */
int
fmt_step_width (enum fmt_type type)
{
  return fmt_get_category (type) & FMT_CAT_HEXADECIMAL ? 2 : 1;
}

/* Returns true if TYPE is used for string fields,
   false if it is used for numeric fields. */
bool
fmt_is_string (enum fmt_type type)
{
  return fmt_get_category (type) & FMT_CAT_STRING;
}

/* Returns true if TYPE is used for numeric fields,
   false if it is used for string fields. */
bool
fmt_is_numeric (enum fmt_type type)
{
  return !fmt_is_string (type);
}

/* Returns the format TYPE's category.
   Each format type is in exactly one category,
   and each category's value is bitwise disjoint from every other
   category.  Thus, the return value may be tested for equality
   or compared bitwise against a mask of FMT_CAT_* values. */
enum fmt_category
fmt_get_category (enum fmt_type type) 
{
  return get_fmt_desc (type)->category;
}

/* Returns the output format selected by default when TYPE is
   used as an input format. */
enum fmt_type
fmt_input_to_output (enum fmt_type type)
{
  enum fmt_category category = fmt_get_category (type);
  return (category & FMT_CAT_STRING ? FMT_A
          : category & (FMT_CAT_BASIC | FMT_CAT_HEXADECIMAL) ? FMT_F
          : type);
}

/* Returns the SPSS format type corresponding to the given PSPP
   format type. */
int
fmt_to_io (enum fmt_type type)
{
  return get_fmt_desc (type)->io;
};

/* Determines the PSPP format corresponding to the given SPSS
   format type.  If successful, sets *FMT_TYPE to the PSPP format
   and returns true.  On failure, return false. */
bool
fmt_from_io (int io, enum fmt_type *fmt_type)
{
  enum fmt_type type;

  for (type = 0; type < FMT_NUMBER_OF_FORMATS; type++)
    if (get_fmt_desc (type)->io == io)
      {
        *fmt_type = type;
        return true;
      }
  return false;
}

/* Returns true if TYPE may be used as an input format,
   false otherwise. */
bool
fmt_usable_for_input (enum fmt_type type)
{
  assert (is_fmt_type (type));
  return fmt_get_category (type) != FMT_CAT_CUSTOM;
}

/* For time and date formats, returns a template used for input
   and output. */
const char *
fmt_date_template (enum fmt_type type)
{
  switch (type)
    {
    case FMT_DATE:
      return "dd-mmm-yy";
    case FMT_ADATE:
      return "mm/dd/yy";
    case FMT_EDATE:
      return "dd.mm.yy";
    case FMT_JDATE:
      return "yyddd";
    case FMT_SDATE:
      return "yy/mm/dd";
    case FMT_QYR:
      return "q Q yy";
    case FMT_MOYR:
      return "mmm yy";
    case FMT_WKYR:
      return "ww WK yy";
    case FMT_DATETIME:
      return "dd-mmm-yyyy HH:MM";
    case FMT_TIME:
      return "h:MM";
    case FMT_DTIME:
      return "D HH:MM";
    default:
      NOT_REACHED ();
    }
}

/* Returns true if TYPE is a valid format type,
   false otherwise. */
static bool
is_fmt_type (enum fmt_type type)
{
  return type < FMT_NUMBER_OF_FORMATS;
}

/* Returns the minimum width of the given format TYPE,
   for input if FOR_INPUT is true,
   for output otherwise. */
static int
min_width (enum fmt_type type, bool for_input)
{
  return for_input ? fmt_min_input_width (type) : fmt_min_output_width (type);
}

/* Returns the maximum width of the given format TYPE,
   which is invariant between input and output. */
static int
max_width (enum fmt_type type)
{
  assert (is_fmt_type (type));
  switch (type)
    {
    case FMT_P:
    case FMT_PK:
    case FMT_PIBHEX:
    case FMT_RBHEX:
      return 16;

    case FMT_IB:
    case FMT_PIB:
    case FMT_RB:
      return 8;

    case FMT_A:
      return MAX_STRING;

    case FMT_AHEX:
      return 2 * MAX_STRING;

    default:
      return 40;
    }
}

/* Returns true if WIDTH is a valid width for the given format
   TYPE,
   for input if FOR_INPUT is true,
   for output otherwise. */
static bool
valid_width (enum fmt_type type, int width, bool for_input)
{
  return (width >= min_width (type, for_input)
          && width <= max_width (type));
}

/* Returns the maximum number of decimal places allowed for the
   given format TYPE with a width of WIDTH places,
   for input if FOR_INPUT is true,
   for output otherwise. */
static int
max_decimals (enum fmt_type type, int width, bool for_input)
{
  int max_d;

  switch (type)
    {
    case FMT_F:
    case FMT_COMMA:
    case FMT_DOT:
      max_d = for_input ? width : width - 1;
      break;

    case FMT_DOLLAR:
    case FMT_PCT:
      max_d = for_input ? width : width - 2;
      break;

    case FMT_E:
      max_d = for_input ? width : width - 7;
      break;

    case FMT_CCA:
    case FMT_CCB:
    case FMT_CCC:
    case FMT_CCD:
    case FMT_CCE:
      assert (!for_input);
      max_d = width - 1;
      break;

    case FMT_N:
    case FMT_Z:
      max_d = width;
      break;

    case FMT_P:
      max_d = width * 2 - 1;
      break;

    case FMT_PK:
      max_d = width * 2;
      break;

    case FMT_IB:
    case FMT_PIB:
      max_d = max_digits_for_bytes (width);
      break;

    case FMT_PIBHEX:
      max_d = 0;
      break;

    case FMT_RB:
    case FMT_RBHEX:
      max_d = 16;
      break;

    case FMT_DATE:
    case FMT_ADATE:
    case FMT_EDATE:
    case FMT_JDATE:
    case FMT_SDATE:
    case FMT_QYR:
    case FMT_MOYR:
    case FMT_WKYR:
      max_d = 0;
      break;

    case FMT_DATETIME:
      max_d = width - 21;
      break;

    case FMT_TIME:
      max_d = width - 9;
      break;

    case FMT_DTIME:
      max_d = width - 12;
      break;

    case FMT_WKDAY:
    case FMT_MONTH:
    case FMT_A:
    case FMT_AHEX:
      max_d = 0;
      break;

    default:
      NOT_REACHED ();
    }

  if (max_d < 0)
    max_d = 0;
  else if (max_d > 16)
    max_d = 16;
  return max_d;
}

/* Returns the maximum number of decimal digits in an unsigned
   binary number that is BYTES bytes long. */
static int
max_digits_for_bytes (int bytes)
{
  int map[8] = {3, 5, 8, 10, 13, 15, 17, 20};
  assert (bytes > 0 && bytes <= sizeof map / sizeof *map);
  return map[bytes - 1];
}

static struct fmt_number_style *styles[FMT_NUMBER_OF_FORMATS];

/* Creates and returns a new struct fmt_number_style,
   initializing all affixes to empty strings. */
struct fmt_number_style *
fmt_number_style_create (void)
{
  struct fmt_number_style *style = xmalloc (sizeof *style);
  style->neg_prefix = ss_empty ();
  style->prefix = ss_empty ();
  style->suffix = ss_empty ();
  style->neg_suffix = ss_empty ();
  style->decimal = '.';
  style->grouping = 0;
  return style;
}

/* Destroys a struct fmt_number_style. */
void
fmt_number_style_destroy (struct fmt_number_style *style)
{
  if (style != NULL)
    {
      ss_dealloc (&style->neg_prefix);
      ss_dealloc (&style->prefix);
      ss_dealloc (&style->suffix);
      ss_dealloc (&style->neg_suffix);
      free (style);
    }
}

/* Returns the number formatting style associated with the given
   format TYPE. */
const struct fmt_number_style *
fmt_get_style (enum fmt_type type)
{
  assert (is_fmt_type (type));
  assert (styles[type] != NULL);
  return styles[type];
}

/* Sets STYLE as the number formatting style associated with the
   given format TYPE, transferring ownership of STYLE.  */
void
fmt_set_style (enum fmt_type type, struct fmt_number_style *style)
{
  assert (ss_length (style->neg_prefix) <= FMT_STYLE_AFFIX_MAX);
  assert (ss_length (style->prefix) <= FMT_STYLE_AFFIX_MAX);
  assert (ss_length (style->suffix) <= FMT_STYLE_AFFIX_MAX);
  assert (ss_length (style->neg_suffix) <= FMT_STYLE_AFFIX_MAX);
  assert (style->decimal == '.' || style->decimal == ',');
  assert (style->grouping != style->decimal
          && (style->grouping == '.' || style->grouping == ','
              || style->grouping == 0));

  assert (fmt_get_category (type) == FMT_CAT_CUSTOM);
  assert (styles[type] != NULL);

  fmt_number_style_destroy (styles[type]);
  styles[type] = style;
}

/* Returns the total width of the standard prefix and suffix for
   STYLE. */
int
fmt_affix_width (const struct fmt_number_style *style)
{
  return ss_length (style->prefix) + ss_length (style->suffix);
}

/* Returns the total width of the negative prefix and suffix for
   STYLE. */
int
fmt_neg_affix_width (const struct fmt_number_style *style)
{
  return ss_length (style->neg_prefix) + ss_length (style->neg_suffix);
}

/* Returns the decimal point character for the given format
   TYPE. */
int
fmt_decimal_char (enum fmt_type type)
{
  return fmt_get_style (type)->decimal;
}

/* Returns the grouping character for the given format TYPE, or 0
   if the format type does not group digits. */
int
fmt_grouping_char (enum fmt_type type)
{
  return fmt_get_style (type)->grouping;
}

/* Sets the number style for TYPE to have the given standard
   PREFIX and SUFFIX, "-" as prefix suffix, an empty negative
   suffix, DECIMAL as the decimal point character, and GROUPING
   as the grouping character. */
static void
set_style (enum fmt_type type,
           const char *prefix, const char *suffix,
           char decimal, char grouping)
{
  struct fmt_number_style *style;

  assert (is_fmt_type (type));

  fmt_number_style_destroy (styles[type]);

  style = styles[type] = fmt_number_style_create ();
  ss_alloc_substring (&style->neg_prefix, ss_cstr ("-"));
  ss_alloc_substring (&style->prefix, ss_cstr (prefix));
  ss_alloc_substring (&style->suffix, ss_cstr (suffix));
  style->decimal = decimal;
  style->grouping = grouping;
}

/* Sets the number style for TYPE as with set_style, but only if
   TYPE has not already been initialized. */
static void
init_style (enum fmt_type type,
            const char *prefix, const char *suffix,
            char decimal, char grouping)
{
  assert (is_fmt_type (type));
  if (styles[type] == NULL)
    set_style (type, prefix, suffix, decimal, grouping);
}

/* Sets the decimal point character to DECIMAL. */
void
fmt_set_decimal (char decimal)
{
  int grouping = decimal == '.' ? ',' : '.';
  assert (decimal == '.' || decimal == ',');

  set_style (FMT_F, "", "", decimal, 0);
  set_style (FMT_E, "", "", decimal, 0);
  set_style (FMT_COMMA, "", "", decimal, grouping);
  set_style (FMT_DOT, "", "", grouping, decimal);
  set_style (FMT_DOLLAR, "$", "", decimal, grouping);
  set_style (FMT_PCT, "", "%", decimal, 0);

  init_style (FMT_CCA, "", "", decimal, grouping);
  init_style (FMT_CCB, "", "", decimal, grouping);
  init_style (FMT_CCC, "", "", decimal, grouping);
  init_style (FMT_CCD, "", "", decimal, grouping);
  init_style (FMT_CCE, "", "", decimal, grouping);
}

/* Returns true if M is a valid variable measurement level,
   false otherwise. */
bool
measure_is_valid (enum measure m)
{
  return m > 0 && m < n_MEASURES;
}

/* Returns true if A is a valid alignment,
   false otherwise. */
bool
alignment_is_valid (enum alignment a)
{
  return a < n_ALIGN;
}

/* Returns the struct fmt_desc for the given format TYPE. */
static const struct fmt_desc *
get_fmt_desc (enum fmt_type type)
{
  static const struct fmt_desc formats[FMT_NUMBER_OF_FORMATS] =
    {
#define FMT(NAME, METHOD, IMIN, OMIN, IO, CATEGORY) \
	{#NAME, IMIN, OMIN, IO, CATEGORY},
#include "format.def"
    };

  assert (is_fmt_type (type));
  return &formats[type];
}
