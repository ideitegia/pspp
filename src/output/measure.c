/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2007, 2009, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include "output/measure.h"

#include <gl/c-strtod.h>
#include <ctype.h>
#include <errno.h>
#if HAVE_LC_PAPER
#include <langinfo.h>
#endif
#include <stdlib.h>

#include "data/file-name.h"
#include "libpspp/str.h"

#include "gl/c-strcase.h"
#include "libpspp/message.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

static double parse_unit (const char *);
static bool parse_paper_size (const char *, int *h, int *v);
static bool get_standard_paper_size (struct substring name, int *h, int *v);
static bool read_paper_conf (const char *file_name, int *h, int *v);
static bool get_default_paper_size (int *h, int *v);

/* Determines the size of a dimensional measurement and returns
   the size in units of 1/72000".  Units are assumed to be
   millimeters unless otherwise specified.  Returns -1 on
   error. */
int
measure_dimension (const char *dimen)
{
  double raw, factor;
  char *tail;

  /* Number. */
  raw = c_strtod (dimen, &tail);
  if (raw < 0.0)
    goto syntax_error;

  /* Unit. */
  factor = parse_unit (tail);
  if (factor == 0.0)
    goto syntax_error;

  return raw * factor;

syntax_error:
  msg (ME, _("`%s' is not a valid length."), dimen);
  return -1;
}

/* Stores the dimensions, in 1/72000" units, of paper identified
   by SIZE into *H and *V.  SIZE can be the name of a kind of
   paper ("a4", "letter", ...) or a pair of dimensions
   ("210x297", "8.5x11in", ...).  Returns true on success, false
   on failure.  On failure, *H and *V are set for A4 paper. */
bool
measure_paper (const char *size, int *h, int *v)
{
  struct substring s;
  bool ok;

  s = ss_cstr (size);
  ss_trim (&s, ss_cstr (CC_SPACES));

  if (ss_is_empty (s))
    {
      /* Treat empty string as default paper size. */
      ok = get_default_paper_size (h, v);
    }
  else if (isdigit (ss_first (s)))
    {
      /* Treat string that starts with digit as explicit size. */
      ok = parse_paper_size (size, h, v);
      if (!ok)
        msg (ME, _("syntax error in paper size `%s'"), size);
    }
  else
    {
      /* Check against standard paper sizes. */
      ok = get_standard_paper_size (s, h, v);
    }

  /* Default to A4 on error. */
  if (!ok)
    {
      *h = 210 * (72000 / 25.4);
      *v = 297 * (72000 / 25.4);
    }
  return ok;
}

/* Parses UNIT as a dimensional unit.  Returns the multiplicative
   factor needed to change a quantity measured in that unit into
   1/72000" units.  If UNIT is empty, it is treated as
   millimeters.  If the unit is unrecognized, returns 0. */
static double
parse_unit (const char *unit)
{
  struct unit
    {
      char name[3];
      double factor;
    };

  static const struct unit units[] =
    {
      {"pt", 72000 / 72},
      {"pc", 72000 / 72 * 12.0},
      {"in", 72000},
      {"cm", 72000 / 2.54},
      {"mm", 72000 / 25.4},
      {"", 72000 / 25.4},
    };

  const struct unit *p;

  unit += strspn (unit, CC_SPACES);
  for (p = units; p < units + sizeof units / sizeof *units; p++)
    if (!c_strcasecmp (unit, p->name))
      return p->factor;
  return 0.0;
}

/* Stores the dimensions in 1/72000" units of paper identified by
   SIZE, which is of form `HORZ x VERT [UNIT]' where HORZ and
   VERT are numbers and UNIT is an optional unit of measurement,
   into *H and *V.  Return true on success. */
static bool
parse_paper_size (const char *size, int *h, int *v)
{
  double raw_h, raw_v, factor;
  char *tail;

  /* Width. */
  raw_h = c_strtod (size, &tail);
  if (raw_h <= 0.0)
    return false;

  /* Delimiter. */
  tail += strspn (tail, CC_SPACES "x,");

  /* Length. */
  raw_v = c_strtod (tail, &tail);
  if (raw_v <= 0.0)
    return false;

  /* Unit. */
  factor = parse_unit (tail);
  if (factor == 0.0)
    return false;

  *h = raw_h * factor + .5;
  *v = raw_v * factor + .5;
  return true;
}

static bool
get_standard_paper_size (struct substring name, int *h, int *v)
{
  static const char *sizes[][2] =
    {
      {"a0", "841 x 1189 mm"},
      {"a1", "594 x 841 mm"},
      {"a2", "420 x 594 mm"},
      {"a3", "297 x 420 mm"},
      {"a4", "210 x 297 mm"},
      {"a5", "148 x 210 mm"},
      {"b5", "176 x 250 mm"},
      {"a6", "105 x 148 mm"},
      {"a7", "74 x 105 mm"},
      {"a8", "52 x 74 mm"},
      {"a9", "37 x 52 mm"},
      {"a10", "26 x 37 mm"},
      {"b0", "1000 x 1414 mm"},
      {"b1", "707 x 1000 mm"},
      {"b2", "500 x 707 mm"},
      {"b3", "353 x 500 mm"},
      {"b4", "250 x 353 mm"},
      {"letter", "612 x 792 pt"},
      {"legal", "612 x 1008 pt"},
      {"executive", "522 x 756 pt"},
      {"note", "612 x 792 pt"},
      {"11x17", "792 x 1224 pt"},
      {"tabloid", "792 x 1224 pt"},
      {"statement", "396 x 612 pt"},
      {"halfletter", "396 x 612 pt"},
      {"halfexecutive", "378 x 522 pt"},
      {"folio", "612 x 936 pt"},
      {"quarto", "610 x 780 pt"},
      {"ledger", "1224 x 792 pt"},
      {"archA", "648 x 864 pt"},
      {"archB", "864 x 1296 pt"},
      {"archC", "1296 x 1728 pt"},
      {"archD", "1728 x 2592 pt"},
      {"archE", "2592 x 3456 pt"},
      {"flsa", "612 x 936 pt"},
      {"flse", "612 x 936 pt"},
      {"csheet", "1224 x 1584 pt"},
      {"dsheet", "1584 x 2448 pt"},
      {"esheet", "2448 x 3168 pt"},
    };

  size_t i;

  for (i = 0; i < sizeof sizes / sizeof *sizes; i++)
    if (ss_equals_case (ss_cstr (sizes[i][0]), name))
      {
        bool ok = parse_paper_size (sizes[i][1], h, v);
        assert (ok);
        return ok;
      }
  msg (ME, _("unknown paper type `%.*s'"),
         (int) ss_length (name), ss_data (name));
  return false;
}

/* Reads file FILE_NAME to find a paper size.  Stores the
   dimensions, in 1/72000" units, into *H and *V.  Returns true
   on success, false on failure. */
static bool
read_paper_conf (const char *file_name, int *h, int *v)
{
  struct string line = DS_EMPTY_INITIALIZER;
  int line_number = 0;
  FILE *file;

  file = fopen (file_name, "r");
  if (file == NULL)
    {
      msg_error (errno, _("error opening input file `%s'"), file_name);
      return false;
    }

  for (;;)
    {
      struct substring name;

      if (!ds_read_config_line (&line, &line_number, file))
	{
	  if (ferror (file))
	    msg_error (errno, _("error reading file `%s'"), file_name);
	  break;
	}

      name = ds_ss (&line);
      ss_trim (&name, ss_cstr (CC_SPACES));
      if (!ss_is_empty (name))
        {
          bool ok = get_standard_paper_size (name, h, v);
          fclose (file);
          ds_destroy (&line);
          return ok;
        }
    }

  fclose (file);
  ds_destroy (&line);
  msg (ME, _("paper size file `%s' does not state a paper size"),
         file_name);
  return false;
}

/* The user didn't specify a paper size, so let's choose a
   default based on his environment.  Stores the
   dimensions, in 1/72000" units, into *H and *V.  Returns true
   on success, false on failure. */
static bool
get_default_paper_size (int *h, int *v)
{
  /* libpaper in Debian (and other distributions?) allows the
     paper size to be specified in $PAPERSIZE or in a file
     specified in $PAPERCONF. */
  if (getenv ("PAPERSIZE") != NULL)
    return get_standard_paper_size (ss_cstr (getenv ("PAPERSIZE")), h, v);
  if (getenv ("PAPERCONF") != NULL)
    return read_paper_conf (getenv ("PAPERCONF"), h, v);

#if HAVE_LC_PAPER
  /* LC_PAPER is a non-standard glibc extension. */
  *h = (int) nl_langinfo(_NL_PAPER_WIDTH) * (72000 / 25.4);
  *v = (int) nl_langinfo(_NL_PAPER_HEIGHT) * (72000 / 25.4);
  if (*h > 0 && *v > 0)
     return true;
#endif

  /* libpaper defaults to /etc/papersize. */
  if (fn_exists ("/etc/papersize"))
    return read_paper_conf ("/etc/papersize", h, v);

  /* Can't find a default. */
  return false;
}

