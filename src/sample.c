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
#include <stdio.h>
#include <math.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "random.h"
#include "str.h"
#include "var.h"

#undef DEBUGGING
/*#define DEBUGGING 1 */
#include "debug-print.h"

/* The two different types of samples. */
enum
  {
    TYPE_A_FROM_B,		/* 5 FROM 10 */
    TYPE_FRACTION		/* 0.5 */
  };

/* SAMPLE transformation. */
struct sample_trns
  {
    struct trns_header h;
    int type;			/* One of TYPE_*. */
    int n, N;			/* TYPE_A_FROM_B: n from N. */
    int m, t;			/* TYPE_A_FROM_B: # selected so far; # so far. */
    int frac;			/* TYPE_FRACTION: a fraction out of 65536. */
  };

int sample_trns_proc (struct trns_header *, struct ccase *);

int
cmd_sample (void)
{
  struct sample_trns *trns;

  int type;
  int a, b;
  int frac;

  lex_match_id ("SAMPLE");

  if (!lex_force_num ())
    return CMD_FAILURE;
  if (!lex_integer_p ())
    {
      type = TYPE_FRACTION;
      if (tokval <= 0 || tokval >= 1)
	{
	  msg (SE, _("The sampling factor must be between 0 and 1 "
		     "exclusive."));
	  return CMD_FAILURE;
	}
	  
      frac = tokval * 65536;
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

#if DEBUGGING
  if (type == TYPE_FRACTION)
    printf ("SAMPLE %g.\n", frac / 65536.);
  else
    printf ("SAMPLE %d FROM %d.\n", a, b);
#endif

  trns = xmalloc (sizeof *trns);
  trns->h.proc = sample_trns_proc;
  trns->h.free = NULL;
  trns->type = type;
  trns->n = a;
  trns->N = b;
  trns->m = trns->t = 0;
  trns->frac = frac;
  add_transformation ((struct trns_header *) trns);

  return lex_end_of_command ();
}

int
sample_trns_proc (struct trns_header * trns, struct ccase *c unused)
{
  struct sample_trns *t = (struct sample_trns *) trns;
  double U;

  if (t->type == TYPE_FRACTION)
    return (rand_simple (0x10000) <= t->frac) - 2;

  if (t->m >= t->n)
    return -2;

  U = rand_uniform (1);
  if ((t->N - t->t) * U >= t->n - t->m)
    {
      t->t++;
      return -2;
    }
  else
    {
      t->m++;
      t->t++;
      return -1;
    }
}
