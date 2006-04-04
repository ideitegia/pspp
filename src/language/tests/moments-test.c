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
#include <stdio.h>
#include "gettext.h"
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <math/moments.h>
#include "xalloc.h"
#include <stdlib.h>
#include <math.h>

#define _(msgid) gettext (msgid)

static int
read_values (double **values, double **weights, size_t *cnt) 
{
  size_t cap = 0;

  *values = NULL;
  *weights = NULL;
  *cnt = 0;
  while (lex_is_number ())
    {
      double value = tokval;
      double weight = 1.;
      lex_get ();
      if (lex_match ('*'))
        {
          if (!lex_is_number ())
            {
              lex_error (_("expecting weight value"));
              return 0;
            }
          weight = tokval;
          lex_get ();
        }

      if (*cnt >= cap) 
        {
          cap = 2 * (cap + 8);
          *values = xnrealloc (*values, cap, sizeof **values);
          *weights = xnrealloc (*weights, cap, sizeof **weights);
        }

      (*values)[*cnt] = value;
      (*weights)[*cnt] = weight;
      (*cnt)++;
    }

  return 1;
}

int
cmd_debug_moments (void) 
{
  int retval = CMD_FAILURE;
  double *values = NULL;
  double *weights = NULL;
  double weight, M[4];
  int two_pass = 1;
  size_t cnt;
  size_t i;

  if (lex_match_id ("ONEPASS"))
    two_pass = 0;
  if (token != '/') 
    {
      lex_force_match ('/');
      goto done;
    }
  fprintf (stderr, "%s => ", lex_rest_of_line (NULL));
  lex_get ();

  if (two_pass) 
    {
      struct moments *m = NULL;
  
      m = moments_create (MOMENT_KURTOSIS);
      if (!read_values (&values, &weights, &cnt)) 
        {
          moments_destroy (m);
          goto done; 
        }
      for (i = 0; i < cnt; i++)
        moments_pass_one (m, values[i], weights[i]); 
      for (i = 0; i < cnt; i++)
        moments_pass_two (m, values[i], weights[i]);
      moments_calculate (m, &weight, &M[0], &M[1], &M[2], &M[3]);
      moments_destroy (m);
    }
  else 
    {
      struct moments1 *m = NULL;
  
      m = moments1_create (MOMENT_KURTOSIS);
      if (!read_values (&values, &weights, &cnt)) 
        {
          moments1_destroy (m);
          goto done; 
        }
      for (i = 0; i < cnt; i++)
        moments1_add (m, values[i], weights[i]);
      moments1_calculate (m, &weight, &M[0], &M[1], &M[2], &M[3]);
      moments1_destroy (m);
    }
  
  fprintf (stderr, "W=%.3f", weight);
  for (i = 0; i < 4; i++) 
    {
      fprintf (stderr, " M%d=", i + 1);
      if (M[i] == SYSMIS)
        fprintf (stderr, "sysmis");
      else if (fabs (M[i]) <= 0.0005)
        fprintf (stderr, "0.000");
      else
        fprintf (stderr, "%.3f", M[i]);
    }
  fprintf (stderr, "\n");

  retval = lex_end_of_command ();
  
 done:
  free (values);
  free (weights);
  return retval;
}
