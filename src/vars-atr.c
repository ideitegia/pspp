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
#include <assert.h>
#include <stdlib.h>
#include "alloc.h"
#include "approx.h"
#include "command.h"
#include "do-ifP.h"
#include "expr.h"
#include "file-handle.h"
#include "hash.h"
#include "inpt-pgm.h"
#include "misc.h"
#include "str.h"
#include "var.h"
#include "vector.h"
#include "value-labels.h"
#include "vfm.h"

#include "debug-print.h"

/* Clear the default dictionary.  Note: This is probably not what you
   want to do.  Use discard_variables() instead. */
void
clear_default_dict (void)
{
  int i;
  
  for (i = 0; i < default_dict.nvar; i++)
    {
      clear_variable (&default_dict, default_dict.var[i]);
      free (default_dict.var[i]);
    }

  assert (default_dict.splits == NULL);

  default_dict.nvar = 0;
  default_dict.N = 0;
  default_dict.nval = 0;
  default_handle = inline_file;
  stop_weighting (&default_dict);
}

/* Discards all the current state in preparation for a data-input
   command like DATA LIST or GET. */
void
discard_variables (void)
{
  clear_default_dict ();
  
  n_lag = 0;
  
  if (vfm_source)
    {
      vfm_source->destroy_source ();
      vfm_source = NULL;
    }

  cancel_transformations ();

  ctl_stack = NULL;

  free (vec);
  vec = NULL;
  nvec = 0;

  expr_free (process_if_expr);
  process_if_expr = NULL;

  cancel_temporary ();

  pgm_state = STATE_INIT;
}

/* Find and return the variable in default_dict having name NAME, or
   NULL if no such variable exists in default_dict. */
struct variable *
find_variable (const char *name)
{
  return hsh_find (default_dict.name_tab, name);
}

/* Find and return the variable in dictionary D having name NAME, or
   NULL if no such variable exists in D. */
struct variable *
find_dict_variable (const struct dictionary *d, const char *name)
{
  return hsh_find (d->name_tab, name);
}

/* Creates a variable named NAME in dictionary DICT having type TYPE
   (ALPHA or NUMERIC) and, if type==ALPHA, width WIDTH.  Returns a
   pointer to the newly created variable if successful.  On failure
   (which indicates that a variable having the specified name already
   exists), returns NULL.  */
struct variable *
create_variable (struct dictionary *dict, const char *name,
		 int type, int width)
{
  if (find_dict_variable (dict, name))
    return NULL;
  
  {
    struct variable *new_var;
    
    dict->var = xrealloc (dict->var, (dict->nvar + 1) * sizeof *dict->var);
    new_var = dict->var[dict->nvar] = xmalloc (sizeof *new_var);
    
    new_var->index = dict->nvar;
    dict->nvar++;
    
    init_variable (dict, new_var, name, type, width);
    
    return new_var;
  }
}

#if GLOBAL_DEBUGGING
/* For situations in which we know that there are no variables with an
   identical name in the dictionary. */
struct variable *
force_create_variable (struct dictionary *dict, const char *name,
		       int type, int width)
{
  struct variable *new_var = create_variable (dict, name, type, width);
  assert (new_var != NULL);
  return new_var;
}

/* For situations in which we know that there are no variables with an
   identical name in the dictionary. */
struct variable *
force_dup_variable (struct dictionary *dict, const struct variable *src,
		    const char *name)
{
  struct variable *new_var = dup_variable (dict, src, name);
  assert (new_var != NULL);
  return new_var;
}
#endif
				 
/* Delete variable V from DICT.  It should only be used when there are
   guaranteed to be absolutely NO REFERENCES to it, for instance in
   the very same function that created it. */
void
delete_variable (struct dictionary *dict, struct variable *v)
{
  int i;

  clear_variable (dict, v);
  dict->nvar--;
  for (i = v->index; i < dict->nvar; i++)
    {
      dict->var[i] = dict->var[i + 1];
      dict->var[i]->index = i;
    }
  free (v);
}

/* Initialize fields in variable V inside dictionary D with name NAME,
   type TYPE, and width WIDTH.  Initializes some other fields too. */
static inline void
common_init_stuff (struct dictionary *dict, struct variable *v,
		   const char *name, int type, int width)
{
  if (v->name != name)
    /* Avoid problems with overlap. */
    strcpy (v->name, name);

  hsh_force_insert (dict->name_tab, v);

  v->type = type;
  v->left = name[0] == '#';
  v->width = type == NUMERIC ? 0 : width;
  v->miss_type = MISSING_NONE;
  if (v->type == NUMERIC)
    {
      v->print.type = FMT_F;
      v->print.w = 8;
      v->print.d = 2;
    }
  else
    {
      v->print.type = FMT_A;
      v->print.w = v->width;
      v->print.d = 0;
    }
  v->write = v->print;
}

/* Initialize (for the first time) a variable V in dictionary DICT
   with name NAME, type TYPE, and width WIDTH.  */
void
init_variable (struct dictionary *dict, struct variable *v, const char *name,
	       int type, int width)
{
  common_init_stuff (dict, v, name, type, width);
  v->nv = type == NUMERIC ? 1 : DIV_RND_UP (width, 8);
  v->fv = dict->nval;
  dict->nval += v->nv;
  v->label = NULL;
  v->val_labs = val_labs_create (width);
  v->get.fv = -1;

  if (vfm_source == &input_program_source
      || vfm_source == &file_type_source)
    {
      size_t nbytes = DIV_RND_UP (v->fv + 1, 4);
      unsigned val = 0;

      if (inp_init_size < nbytes)
	{
	  inp_init = xrealloc (inp_init, nbytes);
	  memset (&inp_init[inp_init_size], 0, nbytes - inp_init_size);
	  inp_init_size = nbytes;
	}

      if (v->type == ALPHA)
	val |= INP_STRING;
      if (v->left)
	val |= INP_LEFT;
      inp_init[v->fv / 4] |= val << ((unsigned) (v->fv) % 4 * 2);
    }
}

/* Replace variable V in default_dict with a different variable having
   name NAME, type TYPE, and width WIDTH. */
void
replace_variable (struct variable *v, const char *name, int type, int width)
{
  int nv;

  assert (v && name && (type == NUMERIC || type == ALPHA) && width >= 0
	  && (type == ALPHA || width == 0));
  clear_variable (&default_dict, v);
  common_init_stuff (&default_dict, v, name, type, width);

  nv = (type == NUMERIC) ? 1 : DIV_RND_UP (width, 8);
  if (nv > v->nv)
    {
      v->fv = v->nv = 0;
      v->fv = default_dict.nval;
      default_dict.nval += nv;
    }
  v->nv = nv;
}

/* Changes the name of variable V in dictionary DICT to name NEW_NAME.
   NEW_NAME must be known not to already exist in dictionary DICT. */
void
rename_variable (struct dictionary * dict, struct variable *v,
		 const char *new_name)
{
  assert (dict && dict->name_tab && v && new_name);
  hsh_delete (dict->name_tab, v);
  strncpy (v->name, new_name, 9);
  hsh_force_insert (dict->name_tab, v);
}

/* Delete the contents of variable V within dictionary DICT.  Does not
   remove the variable from the vector of variables in the dictionary.
   Use with caution. */
void
clear_variable (struct dictionary *dict, struct variable *v)
{
  assert (dict != NULL);
  assert (v != NULL);
  
  if (dict->name_tab != NULL)
    hsh_force_delete (dict->name_tab, v);
  
  val_labs_clear (v->val_labs);
  
  if (v->label)
    {
      free (v->label);
      v->label = NULL;
    }

  if (dict->splits)
    {
      struct variable **iter, **trailer;

      for (trailer = iter = dict->splits; *iter; iter++)
	if (*iter != v)
	  *trailer++ = *iter;
	else
	  dict->n_splits--;

      *trailer = NULL;
      
      if (dict->n_splits == 0)
	{
	  free (dict->splits);
	  dict->splits = NULL;
	}
    }
}

/* Creates a new variable in dictionary DICT, whose properties are
   copied from variable SRC, and returns a pointer to the new variable
   of name NAME, if successful.  If unsuccessful (which only happens
   if a variable of the same name NAME exists in DICT), returns
   NULL. */
struct variable *
dup_variable (struct dictionary *dict, const struct variable *src,
	      const char *name)
{
  if (find_dict_variable (dict, name))
    return NULL;
  
  {
    struct variable *new_var;
    
    dict->var = xrealloc (dict->var, (dict->nvar + 1) * sizeof *dict->var);
    new_var = dict->var[dict->nvar] = xmalloc (sizeof *new_var);

    new_var->index = dict->nvar;
    new_var->foo = -1;
    new_var->get.fv = -1;
    new_var->get.nv = -1;
    dict->nvar++;
    
    copy_variable (new_var, src);

    assert (new_var->nv >= 0);
    new_var->fv = dict->nval;
    dict->nval += new_var->nv;

    strcpy (new_var->name, name);
    hsh_force_insert (dict->name_tab, new_var);

    return new_var;
  }
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
compare_variables (const void *a_, const void *b_, void *foo unused) 
{
  const struct variable *a = a_;
  const struct variable *b = b_;

  return strcmp (a->name, b->name);
}

/* A hsh_hash_func that hashes variable V based on its name. */
unsigned
hash_variable (const void *v_, void *foo unused) 
{
  const struct variable *v = v_;

  return hsh_hash_string (v->name);
}
