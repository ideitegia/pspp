/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010 Free Software Foundation, Inc.

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
#include <stdio.h>
#include "gettext.h"
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <math/moments.h>
#include <math.h>
#include <stdlib.h>
#include "xalloc.h"
#include <libpspp/compiler.h>

#define _(msgid) gettext (msgid)

static bool
read_values (struct lexer *lexer, double **values, double **weights, size_t *cnt)
{
  size_t cap = 0;

  *values = NULL;
  *weights = NULL;
  *cnt = 0;
  while (lex_is_number (lexer))
    {
      double value = lex_tokval (lexer);
      double weight = 1.;
      lex_get (lexer);
      if (lex_match (lexer, T_ASTERISK))
        {
          if (!lex_is_number (lexer))
            {
              lex_error (lexer, _("expecting weight value"));
              return false;
            }
          weight = lex_tokval (lexer);
          lex_get (lexer);
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

  return true;
}

int
cmd_debug_moments (struct lexer *lexer, struct dataset *ds UNUSED)
{
  int retval = CMD_FAILURE;
  double *values = NULL;
  double *weights = NULL;
  double weight, M[4];
  int two_pass = 1;
  size_t cnt;
  size_t i;

  if (lex_match_id (lexer, "ONEPASS"))
    two_pass = 0;
  if (lex_token (lexer) != T_SLASH)
    {
      lex_force_match (lexer, T_SLASH);
      goto done;
    }
  lex_get (lexer);

  if (two_pass)
    {
      struct moments *m = NULL;

      m = moments_create (MOMENT_KURTOSIS);
      if (!read_values (lexer, &values, &weights, &cnt))
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
      if (!read_values (lexer, &values, &weights, &cnt))
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
      fprintf (stderr, " M%zu=", i + 1);
      if (M[i] == SYSMIS)
        fprintf (stderr, "sysmis");
      else if (fabs (M[i]) <= 0.0005)
        fprintf (stderr, "0.000");
      else
        fprintf (stderr, "%.3f", M[i]);
    }
  fprintf (stderr, "\n");

  retval = lex_end_of_command (lexer);

 done:
  free (values);
  free (weights);
  return retval;
}
