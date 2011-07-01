/* PSPP - a program for statistical analysis.
   Copyright (C) 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "data/caseinit.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "data/case.h"
#include "data/dictionary.h"
#include "data/value.h"
#include "data/variable.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"

#include "gl/xalloc.h"

/* Initializer list: a set of values to write to locations within
   a case. */

/* Binds a value with a place to put it. */
struct init_value
  {
    size_t case_index;
    int width;
    union value value;
  };

/* A set of values to initialize in a case. */
struct init_list
  {
    struct init_value *values;
    size_t cnt;
  };

/* A bitmap of the "left" status of variables. */
enum leave_class
  {
    LEAVE_REINIT = 0x001,       /* Reinitialize for every case. */
    LEAVE_LEFT = 0x002          /* Keep the value from one case to the next. */
  };

/* Initializes LIST as an empty initializer list. */
static void
init_list_create (struct init_list *list)
{
  list->values = NULL;
  list->cnt = 0;
}

/* Initializes NEW as a copy of OLD. */
static void
init_list_clone (struct init_list *new, const struct init_list *old)
{
  size_t i;

  new->values = xmemdup (old->values, old->cnt * sizeof *old->values);
  new->cnt = old->cnt;

  for (i = 0; i < new->cnt; i++)
    {
      struct init_value *iv = &new->values[i];
      value_clone (&iv->value, &iv->value, iv->width);
    }
}

/* Frees the storage associated with LIST. */
static void
init_list_destroy (struct init_list *list)
{
  struct init_value *iv;

  for (iv = &list->values[0]; iv < &list->values[list->cnt]; iv++)
    value_destroy (&iv->value, iv->width);
  free (list->values);
}

/* Clears LIST, making it an empty list. */
static void
init_list_clear (struct init_list *list)
{
  init_list_destroy (list);
  init_list_create (list);
}

/* Compares `struct init_value's A and B by case_index and
   returns a strcmp()-type result. */
static int
compare_init_values (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct init_value *a = a_;
  const struct init_value *b = b_;

  return a->case_index < b->case_index ? -1 : a->case_index > b->case_index;
}

/* Returns true if LIST includes CASE_INDEX, false otherwise. */
static bool
init_list_includes (const struct init_list *list, size_t case_index)
{
  struct init_value value;
  value.case_index = case_index;
  return binary_search (list->values, list->cnt, sizeof *list->values,
                        &value, compare_init_values, NULL) != NULL;
}

/* Marks LIST to initialize the `union value's for the variables
   in dictionary D that both (1) fall in the leave class or
   classes designated by INCLUDE and (2) are not in EXCLUDE. */
static void
init_list_mark (struct init_list *list, const struct init_list *exclude,
                enum leave_class include, const struct dictionary *d)
{
  size_t var_cnt = dict_get_var_cnt (d);
  size_t i;

  assert (list != exclude);
  list->values = xnrealloc (list->values, list->cnt + dict_get_var_cnt (d),
                            sizeof *list->values);
  for (i = 0; i < var_cnt; i++)
    {
      struct variable *v = dict_get_var (d, i);
      size_t case_index = var_get_case_index (v);
      struct init_value *iv;

      /* Only include the correct class. */
      if (!(include & (var_get_leave (v) ? LEAVE_LEFT : LEAVE_REINIT)))
        continue;

      /* Don't include those to be excluded. */
      if (exclude != NULL && init_list_includes (exclude, case_index))
        continue;

      iv = &list->values[list->cnt++];
      iv->case_index = case_index;
      iv->width = var_get_width (v);
      value_init (&iv->value, iv->width);
      if (var_is_numeric (v) && var_get_leave (v))
        iv->value.f = 0;
      else
        value_set_missing (&iv->value, iv->width);
    }

  /* Drop duplicates. */
  list->cnt = sort_unique (list->values, list->cnt, sizeof *list->values,
                           compare_init_values, NULL);
}

/* Initializes data in case C to the values in the initializer
   LIST. */
static void
init_list_init (const struct init_list *list, struct ccase *c)
{
  const struct init_value *iv;

  for (iv = &list->values[0]; iv < &list->values[list->cnt]; iv++)
    value_copy (case_data_rw_idx (c, iv->case_index), &iv->value, iv->width);
}

/* Updates the values in the initializer LIST from the data in
   case C. */
static void
init_list_update (const struct init_list *list, const struct ccase *c)
{
  struct init_value *iv;

  for (iv = &list->values[0]; iv < &list->values[list->cnt]; iv++)
    value_copy (&iv->value, case_data_idx (c, iv->case_index), iv->width);
}

/* A case initializer. */
struct caseinit
  {
    /* Values that do not need to be initialized by the
       procedure, because they are initialized by the data
       source. */
    struct init_list preinited_values;

    /* Values that need to be initialized to SYSMIS or spaces in
       each case. */
    struct init_list reinit_values;

    /* Values that need to be initialized to 0 or spaces in the
       first case and thereafter retain their values from case to
       case. */
    struct init_list left_values;
  };

/* Creates and returns a new case initializer. */
struct caseinit *
caseinit_create (void)
{
  struct caseinit *ci = xmalloc (sizeof *ci);
  init_list_create (&ci->preinited_values);
  init_list_create (&ci->reinit_values);
  init_list_create (&ci->left_values);
  return ci;
}

/* Creates and returns a copy of OLD. */
struct caseinit *
caseinit_clone (struct caseinit *old)
{
  struct caseinit *new = xmalloc (sizeof *new);
  init_list_clone (&new->preinited_values, &old->preinited_values);
  init_list_clone (&new->reinit_values, &old->reinit_values);
  init_list_clone (&new->left_values, &old->left_values);
  return new;
}

/* Clears the contents of case initializer CI. */
void
caseinit_clear (struct caseinit *ci)
{
  init_list_clear (&ci->preinited_values);
  init_list_clear (&ci->reinit_values);
  init_list_clear (&ci->left_values);
}

/* Destroys case initializer CI. */
void
caseinit_destroy (struct caseinit *ci)
{
  if (ci != NULL)
    {
      init_list_destroy (&ci->preinited_values);
      init_list_destroy (&ci->reinit_values);
      init_list_destroy (&ci->left_values);
      free (ci);
    }
}

/* Marks the variables from dictionary D in CI as being
   initialized by the data source, so that the case initializer
   need not initialize them itself. */
void
caseinit_mark_as_preinited (struct caseinit *ci, const struct dictionary *d)
{
  init_list_mark (&ci->preinited_values, NULL, LEAVE_REINIT | LEAVE_LEFT, d);
}

/* Marks in CI the variables from dictionary D, except for any
   variables that were already marked with
   caseinit_mark_as_preinited, as needing initialization
   according to their leave status. */
void
caseinit_mark_for_init (struct caseinit *ci, const struct dictionary *d)
{
  init_list_mark (&ci->reinit_values, &ci->preinited_values, LEAVE_REINIT, d);
  init_list_mark (&ci->left_values, &ci->preinited_values, LEAVE_LEFT, d);
}

/* Initializes variables in *C as described by CI.
   C must not be shared. */
void
caseinit_init_vars (const struct caseinit *ci, struct ccase *c)
{
  init_list_init (&ci->reinit_values, c);
  init_list_init (&ci->left_values, c);
}

/* Updates the left vars in CI from the data in C, so that the
   next call to caseinit_init_vars will store those values in the
   next case. */
void
caseinit_update_left_vars (struct caseinit *ci, const struct ccase *c)
{
  init_list_update (&ci->left_values, c);
}

