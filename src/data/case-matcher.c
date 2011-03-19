/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2011 Free Software Foundation, Inc.

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

#include "data/case-matcher.h"

#include <stdlib.h>

#include "data/case.h"
#include "data/subcase.h"
#include "data/value.h"
#include "libpspp/assertion.h"

#include "gl/xalloc.h"

struct case_matcher_input
  {
    struct subcase by_vars;
    struct ccase **data;
    bool *is_minimal;
  };

struct case_matcher
  {
    struct case_matcher_input *inputs;
    size_t n_inputs, allocated_inputs;
    union value *by_values;
  };

/* Creates and returns a new case matcher. */
struct case_matcher *
case_matcher_create (void)
{
  struct case_matcher *cm = xmalloc (sizeof *cm);
  cm->inputs = NULL;
  cm->n_inputs = 0;
  cm->allocated_inputs = 0;
  cm->by_values = NULL;
  return cm;
}

/* Adds a new input file to case matcher CM.
   case_matcher_match() will compare the variables specified in
   BY in case *DATA and set *IS_MINIMAL appropriately.
   (The caller may change the case that *DATA points to from one
   call to the next.)

   All of the BY subcases provided to this function for a given
   CM must be conformable (see subcase_conformable()). */
void
case_matcher_add_input (struct case_matcher *cm, const struct subcase *by,
                        struct ccase **data, bool *is_minimal)
{
  struct case_matcher_input *input;

  if (cm->n_inputs == 0)
    {
      cm->by_values = xmalloc (sizeof *cm->by_values
                               * subcase_get_n_fields (by));
      caseproto_init_values (subcase_get_proto (by), cm->by_values);
    }
  else
    assert (subcase_conformable (by, &cm->inputs[0].by_vars));

  if (cm->n_inputs >= cm->allocated_inputs)
    cm->inputs = x2nrealloc (cm->inputs, &cm->allocated_inputs,
                             sizeof *cm->inputs);
  input = &cm->inputs[cm->n_inputs++];
  subcase_clone (&input->by_vars, by);
  input->data = data;
  input->is_minimal = is_minimal;
}

/* Destroys case matcher CM. */
void
case_matcher_destroy (struct case_matcher *cm)
{
  if (cm != NULL)
    {
      size_t i;

      if (cm->by_values != NULL)
        {
          caseproto_destroy_values (subcase_get_proto (&cm->inputs[0].by_vars),
                                    cm->by_values);
          free (cm->by_values);
        }
      for (i = 0; i < cm->n_inputs; i++)
        {
          struct case_matcher_input *input = &cm->inputs[i];
          subcase_destroy (&input->by_vars);
        }
      free (cm->inputs);
      free (cm);
    }
}

static int
compare_BY_3way (struct case_matcher_input *a, struct case_matcher_input *b)
{
  return subcase_compare_3way (&a->by_vars, *a->data, &b->by_vars, *b->data);
}

/* Compares the values of the BY variables in all of the nonnull
   cases provided to case_matcher_add_input() for CM, sets
   *IS_MINIMAL for each one to true if it has the minimum BY
   values among those cases or to false if its BY values are
   greater than the minimum.  Also sets *IS_MINIMAL to false for
   null cases.  Sets *BY to the BY values extracted from the
   minimum case.  (The caller must not free *BY.)

   Returns true if at least one of the cases is nonnull, false
   if they are all null.*/
bool
case_matcher_match (struct case_matcher *cm, union value **by)
{
  struct case_matcher_input *file, *min;

  min = NULL;
  for (file = cm->inputs; file < &cm->inputs[cm->n_inputs]; file++)
    if (*file->data != NULL)
      {
        int cmp = min != NULL ? compare_BY_3way (min, file) : 1;
        if (cmp < 0)
          *file->is_minimal = false;
        else
          {
            *file->is_minimal = true;
            if (cmp > 0)
              min = file;
          }
      }
    else
      *file->is_minimal = false;

  if (min != NULL)
    {
      for (file = cm->inputs; file < min; file++)
        *file->is_minimal = false;
      subcase_extract (&min->by_vars, *min->data, cm->by_values);
      *by = cm->by_values;
      return true;
    }
  else
    {
      *by = NULL;
      return false;
    }
}
