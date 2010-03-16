/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010 Free Software Foundation, Inc.

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

#include "value-labels.h"

#include <stdlib.h>

#include <data/data-out.h>
#include <data/value.h>
#include <data/variable.h>
#include <libpspp/array.h>
#include <libpspp/cast.h>
#include <libpspp/compiler.h>
#include <libpspp/hash-functions.h>
#include <libpspp/hmap.h>
#include <libpspp/intern.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "xalloc.h"

/* Creates and returns a new, empty set of value labels with the
   given WIDTH. */
struct val_labs *
val_labs_create (int width)
{
  struct val_labs *vls = xmalloc (sizeof *vls);
  vls->width = width;
  hmap_init (&vls->labels);
  return vls;
}

/* Creates and returns a new set of value labels identical to
   VLS.  Returns a null pointer if VLS is null. */
struct val_labs *
val_labs_clone (const struct val_labs *vls)
{
  struct val_labs *copy;
  struct val_lab *label;

  if (vls == NULL)
    return NULL;

  copy = val_labs_create (vls->width);
  HMAP_FOR_EACH (label, struct val_lab, node, &vls->labels)
    val_labs_add (copy, &label->value, label->label);
  return copy;
}

/* Determines whether VLS's width can be changed to NEW_WIDTH,
   using the rules checked by value_is_resizable. */
bool
val_labs_can_set_width (const struct val_labs *vls, int new_width)
{
  struct val_lab *label;

  HMAP_FOR_EACH (label, struct val_lab, node, &vls->labels)
    if (!value_is_resizable (&label->value, vls->width, new_width))
      return false;

  return true;
}

/* Changes the width of VLS to NEW_WIDTH.  The original and new
   width must be both numeric or both string. */
void
val_labs_set_width (struct val_labs *vls, int new_width)
{
  assert (val_labs_can_set_width (vls, new_width));
  if (value_needs_resize (vls->width, new_width))
    {
      struct val_lab *label;
      HMAP_FOR_EACH (label, struct val_lab, node, &vls->labels)
        value_resize (&label->value, vls->width, new_width);
    }
  vls->width = new_width;
}

/* Destroys VLS. */
void
val_labs_destroy (struct val_labs *vls)
{
  if (vls != NULL)
    {
      val_labs_clear (vls);
      hmap_destroy (&vls->labels);
      free (vls);
    }
}

/* Removes all the value labels from VLS. */
void
val_labs_clear (struct val_labs *vls)
{
  struct val_lab *label, *next;

  HMAP_FOR_EACH_SAFE (label, next, struct val_lab, node, &vls->labels)
    {
      hmap_delete (&vls->labels, &label->node);
      value_destroy (&label->value, vls->width);
      intern_unref (label->label);
      free (label);
    }
}

/* Returns the number of value labels in VLS.
   Returns 0 if VLS is null. */
size_t
val_labs_count (const struct val_labs *vls)
{
  return vls == NULL ? 0 : hmap_count (&vls->labels);
}

static void
do_add_val_lab (struct val_labs *vls, const union value *value,
                const char *label)
{
  struct val_lab *lab = xmalloc (sizeof *lab);
  value_clone (&lab->value, value, vls->width);
  lab->label = intern_new (label);
  hmap_insert (&vls->labels, &lab->node, value_hash (value, vls->width, 0));
}

/* If VLS does not already contain a value label for VALUE, adds
   LABEL for it and returns true.  Otherwise, returns false. */
bool
val_labs_add (struct val_labs *vls, const union value *value,
              const char *label)
{
  const struct val_lab *lab = val_labs_lookup (vls, value);
  if (lab == NULL)
    {
      do_add_val_lab (vls, value, label);
      return true;
    }
  else
    return false;
}

/* Sets LABEL as the value label for VALUE in VLS, replacing any
   existing label for VALUE. */
void
val_labs_replace (struct val_labs *vls, const union value *value,
                  const char *label)
{
  struct val_lab *vl = val_labs_lookup (vls, value);
  if (vl != NULL)
    {
      intern_unref (vl->label);
      vl->label = intern_new (label);
    }
  else
    do_add_val_lab (vls, value, label);
}

/* Removes LABEL from VLS. */
void
val_labs_remove (struct val_labs *vls, struct val_lab *label)
{
  hmap_delete (&vls->labels, &label->node);
  value_destroy (&label->value, vls->width);
  intern_unref (label->label);
  free (label);
}

/* Searches VLS for a value label for VALUE.  If successful,
   returns the string used as the label; otherwise, returns a
   null pointer.  Returns a null pointer if VLS is null. */
const char *
val_labs_find (const struct val_labs *vls, const union value *value)
{
  const struct val_lab *label = val_labs_lookup (vls, value);
  return label ? label->label : NULL;
}

/* Searches VLS for a value label for VALUE.  If successful,
   returns the value label; otherwise, returns a null pointer.
   Returns a null pointer if VLS is null. */
struct val_lab *
val_labs_lookup (const struct val_labs *vls, const union value *value)
{
  if (vls != NULL)
    {
      struct val_lab *label;
      HMAP_FOR_EACH_WITH_HASH (label, struct val_lab, node,
                               value_hash (value, vls->width, 0), &vls->labels)
        if (value_equal (&label->value, value, vls->width))
          return label;
    }
  return NULL;
}

/* Returns the first value label in VLS, in arbitrary order, or a
   null pointer if VLS is empty or if VLS is a null pointer.  If
   the return value is non-null, then val_labs_next() may be used
   to continue iterating. */
const struct val_lab *
val_labs_first (const struct val_labs *vls)
{
  return vls ? HMAP_FIRST (struct val_lab, node, &vls->labels) : NULL;
}

/* Returns the next value label in an iteration begun by
   val_labs_first().  If the return value is non-null, then
   val_labs_next() may be used to continue iterating. */
const struct val_lab *
val_labs_next (const struct val_labs *vls, const struct val_lab *label)
{
  return HMAP_NEXT (label, struct val_lab, node, &vls->labels);
}

static int
compare_labels_by_value_3way (const void *a_, const void *b_, const void *vls_)
{
  const struct val_lab *const *a = a_;
  const struct val_lab *const *b = b_;
  const struct val_labs *vls = vls_;
  return value_compare_3way (&(*a)->value, &(*b)->value, vls->width);
}

/* Allocates and returns an array of pointers to value labels
   that is sorted in increasing order by value.  The array has
   val_labs_count(VLS) elements.  The caller is responsible for
   freeing the array. */
const struct val_lab **
val_labs_sorted (const struct val_labs *vls)
{
  if (vls != NULL)
    {
      const struct val_lab *label;
      const struct val_lab **labels;
      size_t i;

      labels = xmalloc (val_labs_count (vls) * sizeof *labels);
      i = 0;
      HMAP_FOR_EACH (label, struct val_lab, node, &vls->labels)
        labels[i++] = label;
      assert (i == val_labs_count (vls));
      sort (labels, val_labs_count (vls), sizeof *labels,
            compare_labels_by_value_3way, vls);
      return labels;
    }
  else
    return NULL;
}
