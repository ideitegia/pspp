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
#include "var.h"
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "dictionary.h"
#include "do-ifP.h"
#include "expr.h"
#include "file-handle.h"
#include "hash.h"
#include "misc.h"
#include "str.h"
#include "value-labels.h"
#include "vfm.h"

#include "debug-print.h"

void *
var_attach_aux (struct variable *v,
                void *aux, void (*aux_dtor) (struct variable *)) 
{
  assert (v->aux == NULL);
  assert (aux != NULL);
  v->aux = aux;
  v->aux_dtor = aux_dtor;
  return aux;
}

void *
var_detach_aux (struct variable *v) 
{
  void *aux = v->aux;
  assert (aux != NULL);
  v->aux = NULL;
  return aux;
}

void
var_clear_aux (struct variable *v) 
{
  assert (v != NULL);
  if (v->aux != NULL) 
    {
      if (v->aux_dtor != NULL)
        v->aux_dtor (v);
      v->aux = NULL;
    }
}

void
var_dtor_free (struct variable *v) 
{
  free (v->aux);
}

/* Compares A and B, which both have the given WIDTH, and returns
   a strcmp()-type result. */
int
compare_values (const union value *a, const union value *b, int width) 
{
  if (width == 0) 
    return a->f < b->f ? -1 : a->f > b->f;
  else
    return memcmp (a->s, b->s, min(MAX_SHORT_STRING, width));
}

/* Create a hash of v */
unsigned 
hash_value(const union value  *v, int width)
{
  unsigned id_hash;

  if ( 0 == width ) 
    id_hash = hsh_hash_double (v->f);
  else
    id_hash = hsh_hash_bytes (v->s, min(MAX_SHORT_STRING, width));

  return id_hash;
}



/* Discards all the current state in preparation for a data-input
   command like DATA LIST or GET. */
void
discard_variables (void)
{
  dict_clear (default_dict);
  default_handle = NULL;

  n_lag = 0;
  
  if (vfm_source != NULL)
    {
      free_case_source (vfm_source);
      vfm_source = NULL;
    }

  cancel_transformations ();

  ctl_stack = NULL;

  expr_free (process_if_expr);
  process_if_expr = NULL;

  cancel_temporary ();

  pgm_state = STATE_INIT;
}

/* Return nonzero only if X is a user-missing value for numeric
   variable V. */
inline int
is_num_user_missing (double x, const struct variable *v)
{
  switch (v->miss_type)
    {
    case MISSING_NONE:
      return 0;
    case MISSING_1:
      return x == v->missing[0].f;
    case MISSING_2:
      return x == v->missing[0].f || x == v->missing[1].f;
    case MISSING_3:
      return (x == v->missing[0].f || x == v->missing[1].f
              || x == v->missing[2].f);
    case MISSING_RANGE:
      return x >= v->missing[0].f && x <= v->missing[1].f;
    case MISSING_LOW:
      return x <= v->missing[0].f;
    case MISSING_HIGH:
      return x >= v->missing[0].f;
    case MISSING_RANGE_1:
      return ((x >= v->missing[0].f && x <= v->missing[1].f)
	      || x == v->missing[2].f);
    case MISSING_LOW_1:
      return x <= v->missing[0].f || x == v->missing[1].f;
    case MISSING_HIGH_1:
      return x >= v->missing[0].f || x == v->missing[1].f;
    default:
      assert (0);
    }
  abort ();
}

/* Return nonzero only if string S is a user-missing variable for
   string variable V. */
inline int
is_str_user_missing (const unsigned char s[], const struct variable *v)
{
  switch (v->miss_type)
    {
    case MISSING_NONE:
      return 0;
    case MISSING_1:
      return !strncmp (s, v->missing[0].s, v->width);
    case MISSING_2:
      return (!strncmp (s, v->missing[0].s, v->width)
	      || !strncmp (s, v->missing[1].s, v->width));
    case MISSING_3:
      return (!strncmp (s, v->missing[0].s, v->width)
	      || !strncmp (s, v->missing[1].s, v->width)
	      || !strncmp (s, v->missing[2].s, v->width));
    default:
      assert (0);
    }
  abort ();
}

/* Return nonzero only if value VAL is system-missing for variable
   V. */
int
is_system_missing (const union value *val, const struct variable *v)
{
  return v->type == NUMERIC && val->f == SYSMIS;
}

/* Return nonzero only if value VAL is system- or user-missing for
   variable V. */
int
is_missing (const union value *val, const struct variable *v)
{
  switch (v->type)
    {
    case NUMERIC:
      if (val->f == SYSMIS)
	return 1;
      return is_num_user_missing (val->f, v);
    case ALPHA:
      return is_str_user_missing (val->s, v);
    default:
      assert (0);
    }
  abort ();
}

/* Return nonzero only if value VAL is user-missing for variable V. */
int
is_user_missing (const union value *val, const struct variable *v)
{
  switch (v->type)
    {
    case NUMERIC:
      return is_num_user_missing (val->f, v);
    case ALPHA:
      return is_str_user_missing (val->s, v);
    default:
      assert (0);
    }
  abort ();
}

/* A hsh_compare_func that orders variables A and B by their
   names. */
int
compare_var_names (const void *a_, const void *b_, void *foo UNUSED) 
{
  const struct variable *a = a_;
  const struct variable *b = b_;

  return strcmp (a->name, b->name);
}

/* A hsh_hash_func that hashes variable V based on its name. */
unsigned
hash_var_name (const void *v_, void *foo UNUSED) 
{
  const struct variable *v = v_;

  return hsh_hash_string (v->name);
}

/* A hsh_compare_func that orders pointers to variables A and B
   by their names. */
int
compare_var_ptr_names (const void *a_, const void *b_, void *foo UNUSED) 
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return strcmp ((*a)->name, (*b)->name);
}

/* A hsh_hash_func that hashes pointer to variable V based on its
   name. */
unsigned
hash_var_ptr_name (const void *v_, void *foo UNUSED) 
{
  struct variable *const *v = v_;

  return hsh_hash_string ((*v)->name);
}
