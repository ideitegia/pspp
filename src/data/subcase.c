/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2011, 2013 Free Software Foundation, Inc.

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

#include "data/subcase.h"

#include <stdlib.h>

#include "data/case.h"
#include "data/variable.h"
#include "libpspp/assertion.h"

#include "gl/xalloc.h"

static void invalidate_proto (struct subcase *sc);

/* Initializes SC as a subcase that contains no fields. */
void
subcase_init_empty (struct subcase *sc)
{
  sc->fields = NULL;
  sc->n_fields = 0;
  sc->proto = NULL;
}

/* Initializes SC as a subcase with fields extracted from the
   N_VARS variables in VARS, with ascending sort order. */
void
subcase_init_vars (struct subcase *sc,
                   const struct variable *const *vars, size_t n_vars)
{
  subcase_init_empty (sc);
  subcase_add_vars_always (sc, vars, n_vars);
}

/* Initializes SC as a subcase with a single field extracted
   from VAR, with the sort order specified by DIRECTION.  */
void
subcase_init_var (struct subcase *sc, const struct variable *var,
                  enum subcase_direction direction)
{
  subcase_init_empty (sc);
  subcase_add_var (sc, var, direction);
}


void
subcase_init (struct subcase *sc, int index, int width,
                  enum subcase_direction direction)
{
  subcase_init_empty (sc);
  subcase_add (sc, index, width, direction);
}


/* Removes all the fields from SC. */
void
subcase_clear (struct subcase *sc)
{
  sc->n_fields = 0;
  invalidate_proto (sc);
}

/* Initializes SC with the same fields as ORIG. */
void
subcase_clone (struct subcase *sc, const struct subcase *orig)
{
  sc->fields = xmemdup (orig->fields, orig->n_fields * sizeof *orig->fields);
  sc->n_fields = orig->n_fields;
  sc->proto = orig->proto ? caseproto_ref (orig->proto) : NULL;
}

/* Frees the memory owned by SC (but not SC itself). */
void
subcase_destroy (struct subcase *sc)
{
  free (sc->fields);
  caseproto_unref (sc->proto);
}

/* Returns true if VAR already has a field in SC,
   false otherwise. */
bool
subcase_contains_var (const struct subcase *sc, const struct variable *var)
{
  return subcase_contains (sc, var_get_case_index (var));
}

/* Returns true if CASE_INDEX already has a field in SC,
   false otherwise. */
bool
subcase_contains (const struct subcase *sc, int case_index)
{
  size_t i;

  for (i = 0; i < sc->n_fields; i++)
    if (sc->fields[i].case_index == case_index)
      return true;

  return false;
}

/* Add a field for VAR to SC, with DIRECTION as the sort order.
   Returns true if successful, false if VAR already has a field
   in SC. */
bool
subcase_add_var (struct subcase *sc, const struct variable *var,
                 enum subcase_direction direction)
{
  if (!subcase_contains_var (sc, var))
    {
      subcase_add_var_always (sc, var, direction);
      return true;
    }
  else
    return false;
}

/* Add a field for CASE_INDEX, WIDTH to SC, with DIRECTION as the sort order.
   Returns true if successful, false if CASE_INDEX already has a field
   in SC. */
bool
subcase_add (struct subcase *sc, int case_index, int width,
             enum subcase_direction direction)
{
  if (!subcase_contains (sc, case_index))
    {
      subcase_add_always (sc, case_index, width, direction);
      return true;
    }
  else
    return false;
}

/* Add a field for VAR to SC, with DIRECTION as the sort order,
   regardless of whether VAR already has a field in SC. */
void
subcase_add_var_always (struct subcase *sc, const struct variable *var,
                        enum subcase_direction direction)
{
  return subcase_add_always (sc, var_get_case_index (var),
                             var_get_width (var), direction);
}

/* Add a field for each of the N_VARS variables in VAR to SC, regardless of
   whether each variable already has a field in SC.  The fields are added with
   ascending direction. */
void
subcase_add_vars_always (struct subcase *sc,
                         const struct variable *const *vars, size_t n_vars)
{
  size_t i;

  sc->fields = xnrealloc (sc->fields,
                          sc->n_fields + n_vars, sizeof *sc->fields);
  for (i = 0; i < n_vars; i++)
    {
      struct subcase_field *field = &sc->fields[sc->n_fields++];
      field->case_index = var_get_case_index (vars[i]);
      field->width = var_get_width (vars[i]);
      field->direction = SC_ASCEND;
    }
  invalidate_proto (sc);
}

/* Add a field for CASE_INDEX, WIDTH to SC, with DIRECTION as the
   sort order, regardless of whether CASE_INDEX already has a
   field in SC. */
void
subcase_add_always (struct subcase *sc, int case_index, int width,
                    enum subcase_direction direction)
{
  struct subcase_field *field;

  sc->fields = xnrealloc (sc->fields, sc->n_fields + 1, sizeof *sc->fields);
  field = &sc->fields[sc->n_fields++];
  field->case_index = case_index;
  field->width = width;
  field->direction = direction;
  invalidate_proto (sc);
}

/* Adds a field to SC for each column in PROTO, so that SC
   contains all of the columns in PROTO in the same order as a
   case conforming to PROTO.  The fields are added with
   ascending direction. */
void
subcase_add_proto_always (struct subcase *sc, const struct caseproto *proto)
{
  size_t n = caseproto_get_n_widths (proto);
  size_t i;

  sc->fields = xnrealloc (sc->fields, sc->n_fields + n, sizeof *sc->fields);
  for (i = 0; i < n; i++)
    {
      struct subcase_field *field = &sc->fields[sc->n_fields++];
      field->case_index = i;
      field->width = caseproto_get_width (proto, i);
      field->direction = SC_ASCEND;
    }
  invalidate_proto (sc);
}

/* Obtains a caseproto for a case described by SC.  The caller
   must not modify or unref the returned case prototype. */
const struct caseproto *
subcase_get_proto (const struct subcase *sc_)
{
  struct subcase *sc = CONST_CAST (struct subcase *, sc_);

  if (sc->proto == NULL)
    {
      size_t i;

      sc->proto = caseproto_create ();
      for (i = 0; i < sc->n_fields; i++)
        sc->proto = caseproto_add_width (sc->proto, sc->fields[i].width);
    }
  return sc->proto;
}

/* Returns true if and only if A and B are conformable, which
   means that they have the same number of fields and that each
   corresponding field in A and B have the same width. */
bool
subcase_conformable (const struct subcase *a, const struct subcase *b)
{
  size_t i;

  if (a == b)
    return true;
  if (a->n_fields != b->n_fields)
    return false;
  for (i = 0; i < a->n_fields; i++)
    if (a->fields[i].width != b->fields[i].width)
      return false;
  return true;
}

/* Copies the fields represented by SC from C into VALUES.
   VALUES must have space for at least subcase_get_n_fields(SC)
   array elements. */
void
subcase_extract (const struct subcase *sc, const struct ccase *c,
                 union value values[])
{
  size_t i;

  for (i = 0; i < sc->n_fields; i++)
    {
      const struct subcase_field *field = &sc->fields[i];
      union value *value = &values[i];
      value_copy (value, case_data_idx (c, field->case_index), field->width);
    }
}

/* Copies the data in VALUES into the fields in C represented by
   SC.  VALUES must have at least subcase_get_n_fields(SC) array
   elements, and C must be large enough to contain all the fields
   in SC. */
void
subcase_inject (const struct subcase *sc,
                const union value values[], struct ccase *c)
{
  size_t i;

  for (i = 0; i < sc->n_fields; i++)
    {
      const struct subcase_field *field = &sc->fields[i];
      const union value *value = &values[i];
      value_copy (case_data_rw_idx (c, field->case_index), value,
                  field->width);
    }
}

/* Copies the fields in SRC represented by SRC_SC into the
   corresponding fields in DST respresented by DST_SC.  SRC_SC
   and DST_SC must be conformable (as tested by
   subcase_conformable()).

   DST must not be shared. */
void
subcase_copy (const struct subcase *src_sc, const struct ccase *src,
              const struct subcase *dst_sc, struct ccase *dst)
{
  size_t i;

  expensive_assert (subcase_conformable (src_sc, dst_sc));
  for (i = 0; i < src_sc->n_fields; i++)
    {
      const struct subcase_field *src_field = &src_sc->fields[i];
      const struct subcase_field *dst_field = &dst_sc->fields[i];
      value_copy (case_data_rw_idx (dst, dst_field->case_index),
                  case_data_idx (src, src_field->case_index),
                  src_field->width);
    }
}

/* Compares the fields in A specified in A_SC against the fields
   in B specified in B_SC.  Returns -1, 0, or 1 if A's fields are
   lexicographically less than, equal to, or greater than B's
   fields, respectively.

   A_SC and B_SC must be conformable (as tested by
   subcase_conformable()). */
int
subcase_compare_3way (const struct subcase *a_sc, const struct ccase *a,
                      const struct subcase *b_sc, const struct ccase *b)
{
  size_t i;

  expensive_assert (subcase_conformable (a_sc, b_sc));
  for (i = 0; i < a_sc->n_fields; i++)
    {
      const struct subcase_field *a_field = &a_sc->fields[i];
      const struct subcase_field *b_field = &b_sc->fields[i];
      int cmp = value_compare_3way (case_data_idx (a, a_field->case_index),
                                    case_data_idx (b, b_field->case_index),
                                    a_field->width);
      if (cmp != 0)
        return a_field->direction == SC_ASCEND ? cmp : -cmp;
    }
  return 0;
}

/* Compares the values in A against the values in B specified by
   SC's fields.  Returns -1, 0, or 1 if A's values are
   lexicographically less than, equal to, or greater than B's
   values, respectively. */
int
subcase_compare_3way_xc (const struct subcase *sc,
                         const union value a[], const struct ccase *b)
{
  size_t i;

  for (i = 0; i < sc->n_fields; i++)
    {
      const struct subcase_field *field = &sc->fields[i];
      int cmp = value_compare_3way (&a[i],
                                    case_data_idx (b, field->case_index),
                                    field->width);
      if (cmp != 0)
        return field->direction == SC_ASCEND ? cmp : -cmp;
    }
  return 0;
}

/* Compares the values in A specified by SC's fields against the
   values in B.  Returns -1, 0, or 1 if A's values are
   lexicographically less than, equal to, or greater than B's
   values, respectively. */
int
subcase_compare_3way_cx (const struct subcase *sc,
                         const struct ccase *a, const union value b[])
{
  return -subcase_compare_3way_xc (sc, b, a);
}

/* Compares the values in A against the values in B, using SC to
   obtain the number and width of each value.  Returns -1, 0, or
   1 if A's values are lexicographically less than, equal to, or
   greater than B's values, respectively. */
int
subcase_compare_3way_xx (const struct subcase *sc,
                         const union value a[], const union value b[])
{
  size_t i;

  for (i = 0; i < sc->n_fields; i++)
    {
      const struct subcase_field *field = &sc->fields[i];
      int cmp = value_compare_3way (a++, b++, field->width);
      if (cmp != 0)
        return field->direction == SC_ASCEND ? cmp : -cmp;
    }
  return 0;
}

/* Compares the fields in A specified in A_SC against the fields
   in B specified in B_SC.  Returns true if the fields' values
   are equal, false otherwise.

   A_SC and B_SC must be conformable (as tested by
   subcase_conformable()). */
bool
subcase_equal (const struct subcase *a_sc, const struct ccase *a,
               const struct subcase *b_sc, const struct ccase *b)
{
  return subcase_compare_3way (a_sc, a, b_sc, b) == 0;
}

/* Compares the values in A against the values in B specified by
   SC's fields.  Returns true if A's values are equal to B's
   values, otherwise false. */
bool
subcase_equal_xc (const struct subcase *sc,
                  const union value a[], const struct ccase *b)
{
  return subcase_compare_3way_xc (sc, a, b) == 0;
}

/* Compares the values in A specified by SC's fields against the
   values in B.  Returns true if A's values are equal to B's
   values, otherwise false. */
bool
subcase_equal_cx (const struct subcase *sc,
                  const struct ccase *a, const union value b[])
{
  return subcase_compare_3way_cx (sc, a, b) == 0;
}

/* Compares the values in A against the values in B, using SC to
   obtain the number and width of each value.  Returns true if
   A's values are equal to B's values, otherwise false. */
bool
subcase_equal_xx (const struct subcase *sc,
                  const union value a[], const union value b[])
{
  return subcase_compare_3way_xx (sc, a, b) == 0;
}

/* Discards SC's case prototype.  (It will be recreated if needed
   again later.) */
static void
invalidate_proto (struct subcase *sc)
{
  caseproto_unref (sc->proto);
  sc->proto = NULL;
}
