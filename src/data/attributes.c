/* PSPP - a program for statistical analysis.
   Copyright (C) 2008, 2009, 2011, 2012 Free Software Foundation, Inc.

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

#include "data/attributes.h"

#include <assert.h>
#include <string.h>

#include "libpspp/array.h"
#include "libpspp/hash-functions.h"

#include "gl/xalloc.h"

/* A custom attribute of the sort maintained by the DATAFILE
   ATTRIBUTE and VARIABLE ATTRIBUTE commands.

   Attributes have a name (the rules for which are the same as
   those for PSPP variable names) and one or more values, each of
   which is a string.  An attribute may be part of one attribute
   set. */
struct attribute
  {
    struct hmap_node node;      /* Used by attrset. */
    char *name;                 /* Name. */
    char **values;              /* Each value. */
    size_t n_values;            /* Number of values. */
    size_t allocated_values;    /* Amount of allocated space for values. */
  };

/* Creates and returns a new attribute with the given NAME.  The
   attribute initially has no values.  (Attributes with no values
   cannot be saved to system files, so at least one value should
   be added before the attribute is made available to the PSPP
   user.) */
struct attribute *
attribute_create (const char *name)
{
  struct attribute *attr = xmalloc (sizeof *attr);
  attr->name = xstrdup (name);
  attr->values = NULL;
  attr->n_values = 0;
  attr->allocated_values = 0;
  return attr;
}

/* Creates and returns a new attribute with the same name and
   values as ORIG. */
struct attribute *
attribute_clone (const struct attribute *orig)
{
  struct attribute *attr;
  size_t i;

  attr = attribute_create (orig->name);
  for (i = 0; i < orig->n_values; i++)
    attribute_add_value (attr, orig->values[i]);
  return attr;
}

/* Destroys ATTR and frees all associated memory.

   This function must not be called if ATTR is part of an
   attribute set.  Use attrset_delete() instead. */
void
attribute_destroy (struct attribute *attr)
{
  if (attr != NULL)
    {
      size_t i;

      for (i = 0; i < attr->n_values; i++)
        free (attr->values[i]);
      free (attr->values);
      free (attr->name);
      free (attr);
    }
}

/* Returns the name of ATTR.  The caller must not free or modify
   the returned string. */
const char *
attribute_get_name (const struct attribute *attr)
{
  return attr->name;
}

/* Returns ATTR's value with the given INDEX, or a null pointer
   if INDEX is greater than or equal to the number of values in
   ATTR (that is, attributes are numbered starting from 0).  The
   caller must not free or modify the returned string.  */
const char *
attribute_get_value (const struct attribute *attr, size_t index)
{
  return index < attr->n_values ? attr->values[index] : NULL;
}

/* Returns ATTR's number of values. */
size_t
attribute_get_n_values (const struct attribute *attrs)
{
  return attrs->n_values;
}

/* Adds a copy of VALUE as a new value to ATTR.  The caller
   retains ownership of VALUE. */
void
attribute_add_value (struct attribute *attr, const char *value)
{
  if (attr->n_values >= attr->allocated_values)
    attr->values = x2nrealloc (attr->values, &attr->allocated_values,
                               sizeof *attr->values);
  attr->values[attr->n_values++] = xstrdup (value);
}

/* Adds or replaces the value with the given INDEX in ATTR by a
   copy of VALUE.  The caller retains ownership of VALUE.

   If INDEX is an existing value index, that value is replaced.
   If no value index numbered INDEX exists in ATTR, then it is
   added, and any values intermediate between the last maximum
   index and INDEX are set to the empty string. */
void
attribute_set_value (struct attribute *attr, size_t index, const char *value)
{
  if (index < attr->n_values)
    {
      /* Replace existing value. */
      free (attr->values[index]);
      attr->values[index] = xstrdup (value);
    }
  else
    {
      /* Add new value. */
      while (index > attr->n_values)
        attribute_add_value (attr, "");
      attribute_add_value (attr, value);
    }

}

/* Deletes the value with the given INDEX from ATTR.  Any values
   with higher-numbered indexes are shifted down into the gap
   that this creates.

   If INDEX is greater than the maximum index, this has no effect.*/
void
attribute_del_value (struct attribute *attr, size_t index)
{
  if (index < attr->n_values)
    {
      free (attr->values[index]);
      remove_element (attr->values, attr->n_values, sizeof *attr->values,
                      index);
      attr->n_values--;
    }
}

/* Initializes SET as a new, initially empty attibute set. */
void
attrset_init (struct attrset *set)
{
  hmap_init (&set->map);
}

/* Initializes NEW_SET as a new attribute set whose contents are
   initially the same as that of OLD_SET. */
void
attrset_clone (struct attrset *new_set, const struct attrset *old_set)
{
  struct attribute *old_attr;

  attrset_init (new_set);
  HMAP_FOR_EACH (old_attr, struct attribute, node, &old_set->map)
    {
      struct attribute *new_attr = attribute_clone (old_attr);
      hmap_insert (&new_set->map, &new_attr->node,
                   hmap_node_hash (&old_attr->node));
    }
}

/* Frees the storage associated with SET, if SET is nonnull.
   (Does not free SET itself.) */
void
attrset_destroy (struct attrset *set)
{
  if (set != NULL)
    {
      struct attribute *attr, *next;

      HMAP_FOR_EACH_SAFE (attr, next, struct attribute, node, &set->map)
        attribute_destroy (attr);
      hmap_destroy (&set->map);
    }
}

/* Returns the number of attributes in SET. */
size_t
attrset_count (const struct attrset *set)
{
  return hmap_count (&set->map);
}

/* Returns the attribute in SET whose name matches NAME
   case-insensitively, or a null pointer if SET does not contain
   an attribute with that name. */
struct attribute *
attrset_lookup (struct attrset *set, const char *name)
{
  struct attribute *attr;
  HMAP_FOR_EACH_WITH_HASH (attr, struct attribute, node,
                           hash_case_string (name, 0), &set->map)
    if (!strcasecmp (attribute_get_name (attr), name))
      break;
  return attr;
}

/* Adds ATTR to SET, which must not already contain an attribute
   with the same name (matched case insensitively).  Ownership of
   ATTR is transferred to SET. */
void
attrset_add (struct attrset *set, struct attribute *attr)
{
  const char *name = attribute_get_name (attr);
  assert (attrset_lookup (set, name) == NULL);
  hmap_insert (&set->map, &attr->node, hash_case_string (name, 0));
}

/* Deletes any attribute from SET that matches NAME
   (case-insensitively). */
void
attrset_delete (struct attrset *set, const char *name)
{
  struct attribute *attr = attrset_lookup (set, name);
  if (attr != NULL)
    {
      hmap_delete (&set->map, &attr->node);
      attribute_destroy (attr);
    }
}

/* Deletes all attributes from SET. */
void
attrset_clear (struct attrset *set)
{
  attrset_destroy (set);
  attrset_init (set);
}

static struct attribute *iterator_data (struct attrset_iterator *iterator)
{
  return HMAP_NULLABLE_DATA (iterator->node, struct attribute, node);
}

/* Returns the first attribute in SET and initializes ITERATOR.
   If SET is empty, returns a null pointer.

   The caller must not destroy the returned attribute, but it may
   add or remove values.

   Attributes are visited in no particular order.  Calling
   attrset_add() during iteration can cause some attributes to
   be visited more than once and others not at all. */
struct attribute *
attrset_first (const struct attrset *set, struct attrset_iterator *iterator)
{
  iterator->node = hmap_first (&set->map);
  return iterator_data (iterator);
}

/* Returns the next attribute in SET and advances ITERATOR, which
   should have been initialized by calling attrset_first().  If
   all the attributes in SET have already been visited, returns a
   null pointer.

   The caller must not destroy the returned attribute, but it may
   add or remove values.

   Attributes are visited in no particular order.  Calling
   attrset_add() during iteration can cause some attributes to
   be visited more than once and others not at all. */
struct attribute *
attrset_next (const struct attrset *set, struct attrset_iterator *iterator)
{
  iterator->node = hmap_next (&set->map, iterator->node);
  return iterator_data (iterator);
}

static int
compare_attribute_by_name (const void *a_, const void *b_)
{
  const struct attribute *const *a = a_;
  const struct attribute *const *b = b_;

  return strcmp ((*a)->name, (*b)->name);
}

/* Allocates and returns an array of pointers to attributes
   that is sorted by attribute name.  The array has
   'attrset_count (SET)' elements.  The caller is responsible for
   freeing the array. */
struct attribute **
attrset_sorted (const struct attrset *set)
{
  if (set != NULL && attrset_count (set) > 0)
    {
      struct attribute **attrs;
      struct attribute *attr;
      size_t i;

      attrs = xmalloc (attrset_count (set) * sizeof *attrs);
      i = 0;
      HMAP_FOR_EACH (attr, struct attribute, node, &set->map)
        attrs[i++] = attr;
      assert (i == attrset_count (set));
      qsort (attrs, attrset_count (set), sizeof *attrs,
             compare_attribute_by_name);
      return attrs;
    }
  else
    return NULL;
}
