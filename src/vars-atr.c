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
#include <assert.h>
#include <stdlib.h>
#include "alloc.h"
#include "approx.h"
#include "command.h"
#include "do-ifP.h"
#include "expr.h"
#include "file-handle.h"
#include "hash.h"
#include "misc.h"
#include "str.h"
#include "value-labels.h"
#include "vfm.h"

#include "debug-print.h"

/* Discards all the current state in preparation for a data-input
   command like DATA LIST or GET. */
void
discard_variables (void)
{
  dict_clear (default_dict);
  default_handle = inline_file;

  n_lag = 0;
  
  if (vfm_source)
    {
      vfm_source->destroy_source ();
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
      return approx_eq (x, v->missing[0].f);
    case MISSING_2:
      return (approx_eq (x, v->missing[0].f)
	      || approx_eq (x, v->missing[1].f));
    case MISSING_3:
      return (approx_eq (x, v->missing[0].f)
	      || approx_eq (x, v->missing[1].f)
	      || approx_eq (x, v->missing[2].f));
    case MISSING_RANGE:
      return (approx_ge (x, v->missing[0].f)
	      && approx_le (x, v->missing[1].f));
    case MISSING_LOW:
      return approx_le (x, v->missing[0].f);
    case MISSING_HIGH:
      return approx_ge (x, v->missing[0].f);
    case MISSING_RANGE_1:
      return ((approx_ge (x, v->missing[0].f)
	       && approx_le (x, v->missing[1].f))
	      || approx_eq (x, v->missing[2].f));
    case MISSING_LOW_1:
      return (approx_le (x, v->missing[0].f)
	      || approx_eq (x, v->missing[1].f));
    case MISSING_HIGH_1:
      return (approx_ge (x, v->missing[0].f)
	      || approx_eq (x, v->missing[1].f));
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
compare_variables (const void *a_, const void *b_, void *foo UNUSED) 
{
  const struct variable *a = a_;
  const struct variable *b = b_;

  return strcmp (a->name, b->name);
}

/* A hsh_hash_func that hashes variable V based on its name. */
unsigned
hash_variable (const void *v_, void *foo UNUSED) 
{
  const struct variable *v = v_;

  return hsh_hash_string (v->name);
}
