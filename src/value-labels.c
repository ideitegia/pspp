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
#include "value-labels.h"
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "hash.h"
#include "str.h"

static hsh_compare_func compare_int_val_lab;
static hsh_hash_func hash_int_val_lab;
static hsh_free_func free_int_val_lab;

struct atom;
static struct atom *atom_create (const char *string);
static void atom_destroy (struct atom *);
static char *atom_to_string (const struct atom *);

/* A set of value labels. */
struct val_labs 
  {
    int width;                  /* 0=numeric, otherwise string width. */
    struct hsh_table *labels;   /* Hash table of `struct int_val_lab's. */
  };

/* Creates and returns a new, empty set of value labels with the
   given WIDTH, which must designate a numeric (0) or short
   string (1...MAX_SHORT_STRING inclusive) width. */
struct val_labs *
val_labs_create (int width) 
{
  struct val_labs *vls;

  assert (width >= 0);

  vls = xmalloc (sizeof *vls);
  vls->width = width;
  vls->labels = NULL;
  return vls;
}

/* Creates and returns a new set of value labels identical to
   VLS. */
struct val_labs *
val_labs_copy (const struct val_labs *vls) 
{
  struct val_labs *copy;
  struct val_labs_iterator *i;
  struct val_lab *vl;

  assert (vls != NULL);

  copy = val_labs_create (vls->width);
  for (vl = val_labs_first (vls, &i); vl != NULL;
       vl = val_labs_next (vls, &i)) 
    val_labs_add (copy, vl->value, vl->label);
  return copy;
}

/* Changes the width of VLS to NEW_WIDTH.  If VLS is numeric,
   NEW_WIDTH must be 0, otherwise it must be within the range
   1...MAX_SHORT_STRING inclusive. */
void
val_labs_set_width (struct val_labs *vls, int new_width) 
{
  assert (vls != NULL);
  assert ((vls->width == 0) == (new_width == 0));

  vls->width = new_width;
}

/* Destroys VLS. */
void
val_labs_destroy (struct val_labs *vls) 
{
  if (vls != NULL) 
    {
      if (vls->labels != NULL)
        hsh_destroy (vls->labels);
      free (vls);
    }
}

/* Removes all the value labels from VLS. */
void
val_labs_clear (struct val_labs *vls) 
{
  assert (vls != NULL);

  hsh_destroy (vls->labels);
  vls->labels = NULL;
}

/* Returns the number of value labels in VLS. */
size_t
val_labs_count (struct val_labs *vls) 
{
  assert (vls != NULL);

  if (vls->labels == NULL)
    return 0;
  else
    return hsh_count (vls->labels);
}

/* One value label in internal format. */
struct int_val_lab
  {
    union value value;          /* The value being labeled. */
    struct atom *label;         /* A ref-counted string. */
  };

/* Creates and returns an int_val_lab based on VALUE and
   LABEL. */
static struct int_val_lab *
create_int_val_lab (struct val_labs *vls, union value value, const char *label) 
{
  struct int_val_lab *ivl;

  assert (label != NULL);
  assert (vls->width <= MAX_SHORT_STRING);
  
  ivl = xmalloc (sizeof *ivl);
  ivl->value = value;
  if (vls->width > 0)
    memset (ivl->value.s + vls->width, ' ', MAX_SHORT_STRING - vls->width);
  ivl->label = atom_create (label);

  return ivl;
}

/* If VLS does not already contain a value label for VALUE, adds
   LABEL for it and returns nonzero.  Otherwise, returns zero.
   Behavior is undefined if VLS's width is greater than
   MAX_SHORT_STRING. */
int
val_labs_add (struct val_labs *vls, union value value, const char *label) 
{
  struct int_val_lab *ivl;
  void **vlpp;

  assert (vls != NULL);
  assert (vls->width <= MAX_SHORT_STRING);
  assert (label != NULL);

  if (vls->labels == NULL) 
    vls->labels = hsh_create (8, compare_int_val_lab, hash_int_val_lab,
                              free_int_val_lab, vls);

  ivl = create_int_val_lab (vls, value, label);
  vlpp = hsh_probe (vls->labels, ivl);
  if (*vlpp == NULL) 
    {
      *vlpp = ivl;
      return 1; 
    }
  else 
    {
      free_int_val_lab (ivl, vls);
      return 0;
    }
}

/* Sets LABEL as the value label for VALUE in VLS.  Returns zero
   if there wasn't already a value label for VALUE, or nonzero if
   there was.  Behavior is undefined if VLS's width is greater
   than MAX_SHORT_STRING. */
int
val_labs_replace (struct val_labs *vls, union value value, const char *label) 
{
  struct int_val_lab *ivl;

  assert (vls != NULL);
  assert (vls->width <= MAX_SHORT_STRING);
  assert (label != NULL);

  if (vls->labels == NULL)
    {
      val_labs_add (vls, value, label);
      return 0;
    }

  ivl = hsh_replace (vls->labels, create_int_val_lab (vls, value, label));
  if (ivl == NULL) 
    return 0;
  else 
    {
      free_int_val_lab (ivl, vls);
      return 1;
    }
}

/* Removes any value label for VALUE within VLS.  Returns nonzero
   if a value label was removed. Behavior is undefined if VLS's
   width is greater than MAX_SHORT_STRING. */
int 
val_labs_remove (struct val_labs *vls, union value value) 
{
  assert (vls != NULL);
  assert (vls->width <= MAX_SHORT_STRING);

  if (vls->labels != NULL) 
    {
      struct int_val_lab *ivl = create_int_val_lab (vls, value, "");
      int deleted = hsh_delete (vls->labels, &ivl);
      free (ivl);
      return deleted;
    }
  else
    return 0;
}

/* Searches VLS for a value label for VALUE.  If successful,
   returns the label; otherwise, returns a null pointer.  If
   VLS's width is greater than MAX_SHORT_STRING, always returns a
   null pointer. */
char *
val_labs_find (const struct val_labs *vls, union value value) 
{
  assert (vls != NULL);

  if (vls->width > MAX_SHORT_STRING)
    return NULL;

  if (vls->labels != NULL) 
    {
      struct int_val_lab ivl, *vlp;

      ivl.value = value;
      vlp = hsh_find (vls->labels, &ivl);
      if (vlp != NULL)
        return atom_to_string (vlp->label);
    }
  return NULL;
}

/* A value labels iterator. */
struct val_labs_iterator 
  {
    void **labels;              /* The labels, in order. */
    void **lp;                  /* Current label. */
    struct val_lab vl;          /* Structure presented to caller. */
  };

/* Sets up *IP for iterating through the value labels in VLS in
   no particular order.  Returns the first value label or a null
   pointer if VLS is empty.  If the return value is non-null,
   then val_labs_next() may be used to continue iterating or
   val_labs_done() to free up the iterator.  Otherwise, neither
   function may be called for *IP. */
struct val_lab *
val_labs_first (const struct val_labs *vls, struct val_labs_iterator **ip) 
{
  struct val_labs_iterator *i;

  assert (vls != NULL);
  assert (ip != NULL);

  if (vls->labels == NULL || vls->width > MAX_SHORT_STRING)
    return NULL;

  i = *ip = xmalloc (sizeof *i);
  i->labels = hsh_data_copy (vls->labels);
  i->lp = i->labels;
  return val_labs_next (vls, ip);
}

/* Sets up *IP for iterating through the value labels in VLS in
   sorted order of values.  Returns the first value label or a
   null pointer if VLS is empty.  If the return value is
   non-null, then val_labs_next() may be used to continue
   iterating or val_labs_done() to free up the iterator.
   Otherwise, neither function may be called for *IP. */
struct val_lab *
val_labs_first_sorted (const struct val_labs *vls,
                       struct val_labs_iterator **ip)
{
  struct val_labs_iterator *i;

  assert (vls != NULL);
  assert (ip != NULL);

  if (vls->labels == NULL || vls->width > MAX_SHORT_STRING)
    return NULL;

  i = *ip = xmalloc (sizeof *i);
  i->lp = i->labels = hsh_sort_copy (vls->labels);
  return val_labs_next (vls, ip);
}

/* Returns the next value label in an iteration begun by
   val_labs_first() or val_labs_first_sorted().  If the return
   value is non-null, then val_labs_next() may be used to
   continue iterating or val_labs_done() to free up the iterator.
   Otherwise, neither function may be called for *IP. */
struct val_lab *
val_labs_next (const struct val_labs *vls, struct val_labs_iterator **ip)
{
  struct val_labs_iterator *i;
  struct int_val_lab *ivl;
  
  assert (vls != NULL);
  assert (vls->width <= MAX_SHORT_STRING);
  assert (ip != NULL);
  assert (*ip != NULL);

  i = *ip;
  ivl = *i->lp++;
  if (ivl != NULL) 
    {
      i->vl.value = ivl->value;
      i->vl.label = atom_to_string (ivl->label);
      return &i->vl;
    }
  else 
    {
      free (i->labels);
      free (i);
      *ip = NULL;
      return NULL;
    }
}

/* Discards the state for an incomplete iteration begun by
   val_labs_first() or val_labs_first_sorted(). */
void 
val_labs_done (struct val_labs_iterator **ip) 
{
  struct val_labs_iterator *i;

  assert (ip != NULL);
  assert (*ip != NULL);
  
  i = *ip;
  free (i->labels);
  free (i);
  *ip = NULL;
}

/* Compares two value labels and returns a strcmp()-type result. */
int
compare_int_val_lab (const void *a_, const void *b_, void *vls_)
{
  const struct int_val_lab *a = a_;
  const struct int_val_lab *b = b_;
  const struct val_labs *vls = vls_;

  if (vls->width == 0) 
    return a->value.f < b->value.f ? -1 : a->value.f > b->value.f;
  else
    return memcmp (a->value.s, b->value.s, vls->width);
}

/* Hash a value label. */
unsigned
hash_int_val_lab (const void *vl_, void *vls_)
{
  const struct int_val_lab *vl = vl_;
  const struct val_labs *vls = vls_;

  if (vls->width == 0)
    return hsh_hash_double (vl->value.f);
  else
    return hsh_hash_bytes (vl->value.s, sizeof vl->value.s);
}

/* Free a value label. */
void
free_int_val_lab (void *vl_, void *vls_ UNUSED) 
{
  struct int_val_lab *vl = vl_;

  atom_destroy (vl->label);
  free (vl);
}

/* Atoms. */

/* An atom. */
struct atom 
  {
    char *string;               /* String value. */
    unsigned ref_count;         /* Number of references. */
  };

static hsh_compare_func compare_atoms;
static hsh_hash_func hash_atom;
static hsh_free_func free_atom;

/* Hash table of atoms. */
static struct hsh_table *atoms;

/* Creates and returns an atom for STRING. */
static struct atom *
atom_create (const char *string) 
{
  struct atom a;
  void **app;
  
  assert (string != NULL);
          
  if (atoms == NULL) 
    atoms = hsh_create (8, compare_atoms, hash_atom, free_atom, NULL);

  a.string = (char *) string;
  app = hsh_probe (atoms, &a);
  if (*app != NULL) 
    {
      struct atom *ap = *app;
      ap->ref_count++;
      return ap;
    }
  else
    {
      struct atom *ap = xmalloc (sizeof *ap);
      ap->string = xstrdup (string);
      ap->ref_count = 1;
      *app = ap;
      return ap;
    }
}

/* Destroys ATOM. */
static void 
atom_destroy (struct atom *atom)
{
  if (atom != NULL) 
    {
      assert (atom->ref_count > 0);
      atom->ref_count--;
      if (atom->ref_count == 0) 
        hsh_force_delete (atoms, atom);
    }
}

/* Returns the string associated with ATOM. */
static  char *
atom_to_string (const struct atom *atom) 
{
  assert (atom != NULL);
  
  return atom->string;
}

/* A hsh_compare_func that compares A and B. */
static int
compare_atoms (const void *a_, const void *b_, void *aux UNUSED) 
{
  const struct atom *a = a_;
  const struct atom *b = b_;

  return strcmp (a->string, b->string);
}

/* A hsh_hash_func that hashes ATOM. */
static unsigned
hash_atom (const void *atom_, void *aux UNUSED) 
{
  const struct atom *atom = atom_;

  return hsh_hash_string (atom->string);
}

/* A hsh_free_func that destroys ATOM. */
static void
free_atom (void *atom_, void *aux UNUSED) 
{
  struct atom *atom = atom_;

  free (atom->string);
  free (atom);
}


/* Get a string representing the value.
   That is, if it has a label, then return that label,
   otherwise, if the value is alpha, then return the string for it,
   else format it and return the formatted string
*/
const char *
value_to_string(const union value *val, const struct variable *var)
{
  static char buf[100];
  char *s;
  const struct val_labs *val_labs = var->val_labs;
  
  s = val_labs_find (val_labs, *val);

  if ( s ) 
    return s;

  if ( 0 == var->width ) 
    snprintf(buf,100,"%g",val->f);
  else
    {
      strncpy(buf,val->s,MAX_SHORT_STRING);
      strcat(buf,"\0");
    }
  return buf;
}
