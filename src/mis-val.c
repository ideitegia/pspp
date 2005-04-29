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
#include "error.h"
#include <stdlib.h>
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "magic.h"
#include "str.h"
#include "var.h"

#include "debug-print.h"

/* Variables on MIS VAL. */
static struct variable **v;
static int nv;

/* Type of the variables on MIS VAL. */
static int type;

/* Width of string variables on MIS VAL. */
static size_t width;

/* Items to fill-in var structs with. */
static int miss_type;
static union value missing[3];

static int parse_varnames (void);
static int parse_numeric (void);
static int parse_alpha (void);

int
cmd_missing_values (void)
{
  int i;

  while (token != '.')
    {
      if (!parse_varnames ())
	goto fail;

      if (token != ')')
	{
	  if ((type == NUMERIC && !parse_numeric ())
	      || (type == ALPHA && !parse_alpha ()))
	    goto fail;
	}
      else
	miss_type = MISSING_NONE;

      if (!lex_match (')'))
	{
	  msg (SE, _("`)' expected after value specification."));
	  goto fail;
	}

      for (i = 0; i < nv; i++)
	{
	  v[i]->miss_type = miss_type;
	  memcpy (v[i]->missing, missing, sizeof v[i]->missing);
	}

      lex_match ('/');
      free (v);
    }

  return lex_end_of_command ();

fail:
  free (v);
  return CMD_PART_SUCCESS_MAYBE;
}

static int
parse_varnames (void)
{
  int i;

  if (!parse_variables (default_dict, &v, &nv, PV_SAME_TYPE))
    return 0;
  if (!lex_match ('('))
    {
      msg (SE, _("`(' expected after variable name%s."), nv > 1 ? "s" : "");
      return 0;
    }

  type = v[0]->type;
  if (type == NUMERIC)
    return 1;

  width = v[0]->width;
  for (i = 1; i < nv; i++)
    if (v[i]->type == ALPHA && v[i]->nv != 1)
      {
	msg (SE, _("Long string value specified."));
	return 0;
      }
    else if (v[i]->type == ALPHA && (int) width != v[i]->width)
      {
	msg (SE, _("Short strings must be of equal width."));
	return 0;
      }

  return 1;
}

/* Number or range? */
enum
  {
    MV_NOR_NOTHING,		/* Empty. */
    MV_NOR_NUMBER,		/* Single number. */
    MV_NOR_RANGE		/* Range. */
  };

/* A single value or a range. */
struct num_or_range
  {
    int type;			/* One of NOR_*. */
    double d[2];		/* d[0]=lower bound or value, d[1]=upper bound. */
  };

/* Parses something of the form <num>, or LO[WEST] THRU <num>, or
   <num> THRU HI[GHEST], or <num> THRU <num>, and sets the appropriate
   members of NOR.  Returns success. */
static int
parse_num_or_range (struct num_or_range * nor)
{
  if (lex_match_id ("LO") || lex_match_id ("LOWEST"))
    {
      nor->type = MV_NOR_RANGE;
      if (!lex_force_match_id ("THRU"))
	return 0;
      if (!lex_force_num ())
	return 0;
      nor->d[0] = LOWEST;
      nor->d[1] = tokval;
    }
  else if (lex_is_number ())
    {
      nor->d[0] = tokval;
      lex_get ();

      if (lex_match_id ("THRU"))
	{
	  nor->type = MV_NOR_RANGE;
	  if (lex_match_id ("HI") || lex_match_id ("HIGHEST"))
	    nor->d[1] = HIGHEST;
	  else
	    {
	      if (!lex_force_num ())
		return 0;
	      nor->d[1] = tokval;
	      lex_get ();

	      if (nor->d[0] > nor->d[1])
		{
		  msg (SE, _("Range %g THRU %g is not valid because %g is "
			     "greater than %g."),
		       nor->d[0], nor->d[1], nor->d[0], nor->d[1]);
		  return 0;
		}
	    }
	}
      else
	nor->type = MV_NOR_NUMBER;
    }
  else
    return -1;

  return 1;
}

/* Parses a set of numeric missing values and stores them into
   `missing[]' and `miss_type' global variables. */
static int
parse_numeric (void)
{
  struct num_or_range set[3];
  int r;

  set[1].type = set[2].type = MV_NOR_NOTHING;

  /* Get first number or range. */
  r = parse_num_or_range (&set[0]);
  if (r < 1)
    {
      if (r == -1)
	msg (SE, _("Number or range expected."));
      return 0;
    }

  /* Get second and third optional number or range. */
  lex_match (',');
  r = parse_num_or_range (&set[1]);
  if (r == 1)
    {
      lex_match (',');
      r = parse_num_or_range (&set[2]);
    }
  if (r == 0)
    return 0;

  /* Force range, if present, into set[0]. */
  if (set[1].type == MV_NOR_RANGE)
    {
      struct num_or_range t = set[1];
      set[1] = set[0];
      set[0] = t;
    }
  if (set[2].type == MV_NOR_RANGE)
    {
      struct num_or_range t = set[2];
      set[2] = set[0];
      set[0] = t;
    }
  
  /* Ensure there's not more than one range, or one range
     plus one value. */
  if (set[1].type == MV_NOR_RANGE || set[2].type == MV_NOR_RANGE)
    {
      msg (SE, _("At most one range can exist in the missing values "
		 "for any one variable."));
      return 0;
    }
  if (set[0].type == MV_NOR_RANGE && set[2].type != MV_NOR_NOTHING)
    {
      msg (SE, _("At most one individual value can be missing along "
		 "with one range."));
      return 0;
    }

  /* Set missing[] from set[]. */
  if (set[0].type == MV_NOR_RANGE)
    {
      int x = 0;

      if (set[0].d[0] == LOWEST)
	{
	  miss_type = MISSING_LOW;
	  missing[x++].f = set[0].d[1];
	}
      else if (set[0].d[1] == HIGHEST)
	{
	  miss_type = MISSING_HIGH;
	  missing[x++].f = set[0].d[0];
	}
      else
	{
	  miss_type = MISSING_RANGE;
	  missing[x++].f = set[0].d[0];
	  missing[x++].f = set[0].d[1];
	}

      if (set[1].type == MV_NOR_NUMBER)
	{
	  miss_type += 3;
	  missing[x].f = set[1].d[0];
	}
    }
  else
    {
      if (set[0].type == MV_NOR_NUMBER)
	{
	  miss_type = MISSING_1;
	  missing[0].f = set[0].d[0];
	}
      if (set[1].type == MV_NOR_NUMBER)
	{
	  miss_type = MISSING_2;
	  missing[1].f = set[1].d[0];
	}
      if (set[2].type == MV_NOR_NUMBER)
	{
	  miss_type = MISSING_3;
	  missing[2].f = set[2].d[0];
	}
    }

  return 1;
}

static int
parse_alpha (void)
{
  for (miss_type = 0; token == T_STRING && miss_type < 3; miss_type++)
    {
      if (ds_length (&tokstr) != width)
	{
	  msg (SE, _("String is not of proper length."));
	  return 0;
	}
      strncpy (missing[miss_type].s, ds_c_str (&tokstr), MAX_SHORT_STRING);
      lex_get ();
      lex_match (',');
    }
  if (miss_type < 1)
    {
      msg (SE, _("String expected."));
      return 0;
    }

  return 1;
}

/* Copy the missing values from variable SRC to variable DEST. */
void
copy_missing_values (struct variable *dest, const struct variable *src)
{
  static const int n_values[MISSING_COUNT] = 
    {
      0, 1, 2, 3, 2, 1, 1, 3, 2, 2,
    };
    
  assert (dest->width == src->width);
  assert (src->miss_type >= 0 && src->miss_type < MISSING_COUNT);
  
  {
    int i;

    dest->miss_type = src->miss_type;
    for (i = 0; i < n_values[src->miss_type]; i++)
      if (src->type == NUMERIC)
	dest->missing[i].f = src->missing[i].f;
      else
	memcpy (dest->missing[i].s, src->missing[i].s, src->width);
  }
}
