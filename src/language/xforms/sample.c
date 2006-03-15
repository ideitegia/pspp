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
#include <gsl/gsl_rng.h>
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <libpspp/alloc.h>
#include <language/command.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <language/lexer/lexer.h>
#include <math/random.h>
#include <libpspp/str.h>
#include <data/variable.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

#include <libpspp/debug-print.h>

/* The two different types of samples. */
enum
  {
    TYPE_A_FROM_B,		/* 5 FROM 10 */
    TYPE_FRACTION		/* 0.5 */
  };

/* SAMPLE transformation. */
struct sample_trns
  {
    int type;			/* One of TYPE_*. */
    int n, N;			/* TYPE_A_FROM_B: n from N. */
    int m, t;			/* TYPE_A_FROM_B: # picked so far; # so far. */
    unsigned frac;              /* TYPE_FRACTION: a fraction of UINT_MAX. */
  };

static trns_proc_func sample_trns_proc;
static trns_free_func sample_trns_free;

int
cmd_sample (void)
{
  struct sample_trns *trns;

  int type;
  int a, b;
  unsigned frac;

  if (!lex_force_num ())
    return CMD_FAILURE;
  if (!lex_is_integer ())
    {
      unsigned long min = gsl_rng_min (get_rng ());
      unsigned long max = gsl_rng_max (get_rng ());

      type = TYPE_FRACTION;
      if (tokval <= 0 || tokval >= 1)
	{
	  msg (SE, _("The sampling factor must be between 0 and 1 "
		     "exclusive."));
	  return CMD_FAILURE;
	}
	  
      frac = tokval * (max - min) + min;
      a = b = 0;
    }
  else
    {
      type = TYPE_A_FROM_B;
      a = lex_integer ();
      lex_get ();
      if (!lex_force_match_id ("FROM"))
	return CMD_FAILURE;
      if (!lex_force_int ())
	return CMD_FAILURE;
      b = lex_integer ();
      if (a >= b)
	{
	  msg (SE, _("Cannot sample %d observations from a population of "
		     "%d."),
	       a, b);
	  return CMD_FAILURE;
	}
      
      frac = 0;
    }
  lex_get ();

  trns = xmalloc (sizeof *trns);
  trns->type = type;
  trns->n = a;
  trns->N = b;
  trns->m = trns->t = 0;
  trns->frac = frac;
  add_transformation (sample_trns_proc, sample_trns_free, trns);

  return lex_end_of_command ();
}

/* Executes a SAMPLE transformation. */
static int
sample_trns_proc (void *t_, struct ccase *c UNUSED,
                  int case_num UNUSED)
{
  struct sample_trns *t = t_;
  double U;

  if (t->type == TYPE_FRACTION) 
    {
      if (gsl_rng_get (get_rng ()) <= t->frac)
        return TRNS_CONTINUE;
      else
        return TRNS_DROP_CASE;
    }

  if (t->m >= t->n)
    return TRNS_DROP_CASE;

  U = gsl_rng_uniform (get_rng ());
  if ((t->N - t->t) * U >= t->n - t->m)
    {
      t->t++;
      return TRNS_DROP_CASE;
    }
  else
    {
      t->m++;
      t->t++;
      return TRNS_CONTINUE;
    }
}

static bool
sample_trns_free (void *t_) 
{
  struct sample_trns *t = t_;
  free (t);
  return true;
}
