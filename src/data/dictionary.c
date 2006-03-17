/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.
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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "dictionary.h"
#include <stdlib.h>
#include <ctype.h>
#include <libpspp/array.h>
#include <libpspp/alloc.h>
#include "case.h"
#include "category.h"
#include "cat-routines.h"
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/hash.h>
#include <libpspp/misc.h>
#include "settings.h"
#include <libpspp/str.h>
#include "value-labels.h"
#include "variable.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A dictionary. */
struct dictionary
  {
    struct variable **var;	/* Variables. */
    size_t var_cnt, var_cap;    /* Number of variables, capacity. */
    struct hsh_table *name_tab;	/* Variable index by name. */
    int next_value_idx;         /* Index of next `union value' to allocate. */
    struct variable **split;    /* SPLIT FILE vars. */
    size_t split_cnt;           /* SPLIT FILE count. */
    struct variable *weight;    /* WEIGHT variable. */
    struct variable *filter;    /* FILTER variable. */
    int case_limit;             /* Current case limit (N command). */
    char *label;		/* File label. */
    char *documents;		/* Documents, as a string. */
    struct vector **vector;     /* Vectors of variables. */
    size_t vector_cnt;          /* Number of vectors. */
  };

/* Active file dictionary. */
struct dictionary *default_dict;

/* Creates and returns a new dictionary. */
struct dictionary *
dict_create (void) 
{
  struct dictionary *d = xmalloc (sizeof *d);
  
  d->var = NULL;
  d->var_cnt = d->var_cap = 0;
  d->name_tab = hsh_create (8, compare_var_names, hash_var_name, NULL, NULL);
  d->next_value_idx = 0;
  d->split = NULL;
  d->split_cnt = 0;
  d->weight = NULL;
  d->filter = NULL;
  d->case_limit = 0;
  d->label = NULL;
  d->documents = NULL;
  d->vector = NULL;
  d->vector_cnt = 0;

  return d;
}

/* Creates and returns a (deep) copy of an existing
   dictionary. */
struct dictionary *
dict_clone (const struct dictionary *s) 
{
  struct dictionary *d;
  size_t i;

  assert (s != NULL);

  d = dict_create ();

  for (i = 0; i < s->var_cnt; i++) 
    {
      struct variable *sv = s->var[i];
      struct variable *dv = dict_clone_var_assert (d, sv, sv->name);
      var_set_short_name (dv, sv->short_name);
    }

  d->next_value_idx = s->next_value_idx;

  d->split_cnt = s->split_cnt;
  if (d->split_cnt > 0) 
    {
      d->split = xnmalloc (d->split_cnt, sizeof *d->split);
      for (i = 0; i < d->split_cnt; i++) 
        d->split[i] = dict_lookup_var_assert (d, s->split[i]->name);
    }

  if (s->weight != NULL) 
    d->weight = dict_lookup_var_assert (d, s->weight->name);

  if (s->filter != NULL) 
    d->filter = dict_lookup_var_assert (d, s->filter->name);

  d->case_limit = s->case_limit;
  dict_set_label (d, dict_get_label (s));
  dict_set_documents (d, dict_get_documents (s));

  d->vector_cnt = s->vector_cnt;
  d->vector = xnmalloc (d->vector_cnt, sizeof *d->vector);
  for (i = 0; i < s->vector_cnt; i++) 
    {
      struct vector *sv = s->vector[i];
      struct vector *dv = d->vector[i] = xmalloc (sizeof *dv);
      int j;
      
      dv->idx = i;
      strcpy (dv->name, sv->name);
      dv->cnt = sv->cnt;
      dv->var = xnmalloc (dv->cnt, sizeof *dv->var);
      for (j = 0; j < dv->cnt; j++)
        dv->var[j] = d->var[sv->var[j]->index];
    }

  return d;
}

/* Clears the contents from a dictionary without destroying the
   dictionary itself. */
void
dict_clear (struct dictionary *d) 
{
  /* FIXME?  Should we really clear case_limit, label, documents?
     Others are necessarily cleared by deleting all the variables.*/
  int i;

  assert (d != NULL);

  for (i = 0; i < d->var_cnt; i++) 
    {
      struct variable *v = d->var[i];
      var_clear_aux (v);
      val_labs_destroy (v->val_labs);
      free (v->label);
      free (v); 
    }
  free (d->var);
  d->var = NULL;
  d->var_cnt = d->var_cap = 0;
  hsh_clear (d->name_tab);
  d->next_value_idx = 0;
  free (d->split);
  d->split = NULL;
  d->split_cnt = 0;
  d->weight = NULL;
  d->filter = NULL;
  d->case_limit = 0;
  free (d->label);
  d->label = NULL;
  free (d->documents);
  d->documents = NULL;
  dict_clear_vectors (d);
}

/* Destroys the aux data for every variable in D, by calling
   var_clear_aux() for each variable. */
void
dict_clear_aux (struct dictionary *d) 
{
  int i;
  
  assert (d != NULL);
  
  for (i = 0; i < d->var_cnt; i++)
    var_clear_aux (d->var[i]);
}

/* Clears a dictionary and destroys it. */
void
dict_destroy (struct dictionary *d)
{
  if (d != NULL) 
    {
      dict_clear (d);
      hsh_destroy (d->name_tab);
      free (d);
    }
}

/* Returns the number of variables in D. */
size_t
dict_get_var_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->var_cnt;
}

/* Returns the variable in D with index IDX, which must be
   between 0 and the count returned by dict_get_var_cnt(),
   exclusive. */
struct variable *
dict_get_var (const struct dictionary *d, size_t idx) 
{
  assert (d != NULL);
  assert (idx < d->var_cnt);

  return d->var[idx];
}

/* Sets *VARS to an array of pointers to variables in D and *CNT
   to the number of variables in *D.  All variables are returned
   if EXCLUDE_CLASSES is 0, or it may contain one or more of (1u
   << DC_ORDINARY), (1u << DC_SYSTEM), or (1u << DC_SCRATCH) to
   exclude the corresponding type of variable. */
void
dict_get_vars (const struct dictionary *d, struct variable ***vars,
               size_t *cnt, unsigned exclude_classes)
{
  size_t count;
  size_t i;
  
  assert (d != NULL);
  assert (vars != NULL);
  assert (cnt != NULL);
  assert ((exclude_classes & ~((1u << DC_ORDINARY)
                               | (1u << DC_SYSTEM)
                               | (1u << DC_SCRATCH))) == 0);
  
  count = 0;
  for (i = 0; i < d->var_cnt; i++)
    if (!(exclude_classes & (1u << dict_class_from_id (d->var[i]->name))))
      count++;

  *vars = xnmalloc (count, sizeof **vars);
  *cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    if (!(exclude_classes & (1u << dict_class_from_id (d->var[i]->name))))
      (*vars)[(*cnt)++] = d->var[i];
  assert (*cnt == count);
}


/* Creates and returns a new variable in D with the given NAME
   and WIDTH.  Returns a null pointer if the given NAME would
   duplicate that of an existing variable in the dictionary. */
struct variable *
dict_create_var (struct dictionary *d, const char *name, int width)
{
  struct variable *v;

  assert (d != NULL);
  assert (name != NULL);

  assert (width >= 0 && width < 256);

  assert (var_is_plausible_name(name,0));
    
  /* Make sure there's not already a variable by that name. */
  if (dict_lookup_var (d, name) != NULL)
    return NULL;

  /* Allocate and initialize variable. */
  v = xmalloc (sizeof *v);
  str_copy_trunc (v->name, sizeof v->name, name);
  v->type = width == 0 ? NUMERIC : ALPHA;
  v->width = width;
  v->fv = d->next_value_idx;
  v->nv = width == 0 ? 1 : DIV_RND_UP (width, 8);
  v->init = 1;
  v->reinit = dict_class_from_id (v->name) != DC_SCRATCH;
  v->index = d->var_cnt;
  mv_init (&v->miss, width);
  if (v->type == NUMERIC)
    {
      v->print = f8_2;
      v->alignment = ALIGN_RIGHT;
      v->display_width = 8;
      v->measure = MEASURE_SCALE;
    }
  else
    {
      v->print = make_output_format (FMT_A, v->width, 0);
      v->alignment = ALIGN_LEFT;
      v->display_width = 8;
      v->measure = MEASURE_NOMINAL;
    }
  v->write = v->print;
  v->val_labs = val_labs_create (v->width);
  v->label = NULL;
  var_clear_short_name (v);
  v->aux = NULL;
  v->aux_dtor = NULL;
  v->obs_vals = NULL;

  /* Update dictionary. */
  if (d->var_cnt >= d->var_cap) 
    {
      d->var_cap = 8 + 2 * d->var_cap; 
      d->var = xnrealloc (d->var, d->var_cap, sizeof *d->var);
    }
  d->var[v->index] = v;
  d->var_cnt++;
  hsh_force_insert (d->name_tab, v);

  d->next_value_idx += v->nv;

  return v;
}

/* Creates and returns a new variable in D with the given NAME
   and WIDTH.  Assert-fails if the given NAME would duplicate
   that of an existing variable in the dictionary. */
struct variable *
dict_create_var_assert (struct dictionary *d, const char *name, int width)
{
  struct variable *v = dict_create_var (d, name, width);
  assert (v != NULL);
  return v;
}

/* Creates and returns a new variable in D with name NAME, as a
   copy of existing variable OV, which need not be in D or in any
   dictionary.  Returns a null pointer if the given NAME would
   duplicate that of an existing variable in the dictionary. */
struct variable *
dict_clone_var (struct dictionary *d, const struct variable *ov,
                const char *name)
{
  struct variable *nv;

  assert (d != NULL);
  assert (ov != NULL);
  assert (name != NULL);

  assert (strlen (name) >= 1);
  assert (strlen (name) <= LONG_NAME_LEN);

  nv = dict_create_var (d, name, ov->width);
  if (nv == NULL)
    return NULL;

  /* Copy most members not copied via dict_create_var().
     short_name[] is intentionally not copied, because there is
     no reason to give a new variable with potentially a new name
     the same short name. */
  nv->init = 1;
  nv->reinit = ov->reinit;
  mv_copy (&nv->miss, &ov->miss);
  nv->print = ov->print;
  nv->write = ov->write;
  val_labs_destroy (nv->val_labs);
  nv->val_labs = val_labs_copy (ov->val_labs);
  if (ov->label != NULL)
    nv->label = xstrdup (ov->label);
  nv->measure = ov->measure;
  nv->display_width = ov->display_width;
  nv->alignment = ov->alignment;

  return nv;
}

/* Creates and returns a new variable in D with name NAME, as a
   copy of existing variable OV, which need not be in D or in any
   dictionary.  Assert-fails if the given NAME would duplicate
   that of an existing variable in the dictionary. */
struct variable *
dict_clone_var_assert (struct dictionary *d, const struct variable *ov,
                       const char *name)
{
  struct variable *v = dict_clone_var (d, ov, name);
  assert (v != NULL);
  return v;
}

/* Returns the variable named NAME in D, or a null pointer if no
   variable has that name. */
struct variable *
dict_lookup_var (const struct dictionary *d, const char *name)
{
  struct variable v;
  
  assert (d != NULL);
  assert (name != NULL);

  str_copy_trunc (v.name, sizeof v.name, name);
  return hsh_find (d->name_tab, &v);
}

/* Returns the variable named NAME in D.  Assert-fails if no
   variable has that name. */
struct variable *
dict_lookup_var_assert (const struct dictionary *d, const char *name)
{
  struct variable *v = dict_lookup_var (d, name);
  assert (v != NULL);
  return v;
}

/* Returns true if variable V is in dictionary D,
   false otherwise. */
bool
dict_contains_var (const struct dictionary *d, const struct variable *v)
{
  assert (d != NULL);
  assert (v != NULL);

  return v->index >= 0 && v->index < d->var_cnt && d->var[v->index] == v;
}

/* Compares two double pointers to variables, which should point
   to elements of a struct dictionary's `var' member array. */
static int
compare_var_ptrs (const void *a_, const void *b_, void *aux UNUSED) 
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Deletes variable V from dictionary D and frees V.

   This is a very bad idea if there might be any pointers to V
   from outside D.  In general, no variable in default_dict
   should be deleted when any transformations are active, because
   those transformations might reference the deleted variable.
   The safest time to delete a variable is just after a procedure
   has been executed, as done by MODIFY VARS.

   Pointers to V within D are not a problem, because
   dict_delete_var() knows to remove V from split variables,
   weights, filters, etc. */
void
dict_delete_var (struct dictionary *d, struct variable *v) 
{
  size_t i;

  assert (d != NULL);
  assert (v != NULL);
  assert (dict_contains_var (d, v));

  /* Delete aux data. */
  var_clear_aux (v);

  /* Remove V from splits, weight, filter variables. */
  d->split_cnt = remove_equal (d->split, d->split_cnt, sizeof *d->split,
                               &v, compare_var_ptrs, NULL);
  if (d->weight == v)
    d->weight = NULL;
  if (d->filter == v)
    d->filter = NULL;
  dict_clear_vectors (d);

  /* Remove V from var array. */
  remove_element (d->var, d->var_cnt, sizeof *d->var, v->index);
  d->var_cnt--;

  /* Update index. */
  for (i = v->index; i < d->var_cnt; i++)
    d->var[i]->index = i;

  /* Update name hash. */
  hsh_force_delete (d->name_tab, v);

  /* Free memory. */
  val_labs_destroy (v->val_labs);
  cat_stored_values_destroy (v);
  free (v->label);
  free (v);
}

/* Deletes the COUNT variables listed in VARS from D.  This is
   unsafe; see the comment on dict_delete_var() for details. */
void 
dict_delete_vars (struct dictionary *d,
                  struct variable *const *vars, size_t count) 
{
  /* FIXME: this can be done in O(count) time, but this algorithm
     is O(count**2). */
  assert (d != NULL);
  assert (count == 0 || vars != NULL);

  while (count-- > 0)
    dict_delete_var (d, *vars++);
}

/* Deletes scratch variables from dictionary D. */
void
dict_delete_scratch_vars (struct dictionary *d)
{
  int i;

  /* FIXME: this can be done in O(count) time, but this algorithm
     is O(count**2). */
  assert (d != NULL);

  for (i = 0; i < d->var_cnt; )
    if (dict_class_from_id (d->var[i]->name) == DC_SCRATCH)
      dict_delete_var (d, d->var[i]);
    else
      i++;
}

/* Moves V to 0-based position IDX in D.  Other variables in D,
   if any, retain their relative positions.  Runs in time linear
   in the distance moved. */
void
dict_reorder_var (struct dictionary *d, struct variable *v,
                  size_t new_index) 
{
  size_t min_idx, max_idx;
  size_t i;
  
  assert (d != NULL);
  assert (v != NULL);
  assert (dict_contains_var (d, v));
  assert (new_index < d->var_cnt);

  move_element (d->var, d->var_cnt, sizeof *d->var, v->index, new_index);

  min_idx = min (v->index, new_index);
  max_idx = max (v->index, new_index);
  for (i = min_idx; i <= max_idx; i++)
    d->var[i]->index = i;
}

/* Reorders the variables in D, placing the COUNT variables
   listed in ORDER in that order at the beginning of D.  The
   other variables in D, if any, retain their relative
   positions. */
void 
dict_reorder_vars (struct dictionary *d,
                   struct variable *const *order, size_t count) 
{
  struct variable **new_var;
  size_t i;
  
  assert (d != NULL);
  assert (count == 0 || order != NULL);
  assert (count <= d->var_cnt);

  new_var = xnmalloc (d->var_cnt, sizeof *new_var);
  memcpy (new_var, order, count * sizeof *new_var);
  for (i = 0; i < count; i++) 
    {
      assert (d->var[order[i]->index] != NULL);
      d->var[order[i]->index] = NULL;
      order[i]->index = i;
    }
  for (i = 0; i < d->var_cnt; i++)
    if (d->var[i] != NULL)
      {
        assert (count < d->var_cnt);
        new_var[count] = d->var[i];
        new_var[count]->index = count;
        count++;
      }
  free (d->var);
  d->var = new_var;
}

/* Changes the name of V in D to name NEW_NAME.  Assert-fails if
   a variable named NEW_NAME is already in D, except that
   NEW_NAME may be the same as V's existing name. */
void 
dict_rename_var (struct dictionary *d, struct variable *v,
                 const char *new_name) 
{
  assert (d != NULL);
  assert (v != NULL);
  assert (new_name != NULL);
  assert (var_is_plausible_name (new_name, false));
  assert (dict_contains_var (d, v));
  assert (!compare_var_names (v->name, new_name, NULL)
          || dict_lookup_var (d, new_name) == NULL);

  hsh_force_delete (d->name_tab, v);
  str_copy_trunc (v->name, sizeof v->name, new_name);
  hsh_force_insert (d->name_tab, v);

  if (get_algorithm () == ENHANCED)
    var_clear_short_name (v);
}

/* Renames COUNT variables specified in VARS to the names given
   in NEW_NAMES within dictionary D.  If the renaming would
   result in a duplicate variable name, returns false and stores a
   name that would be duplicated into *ERR_NAME (if ERR_NAME is
   non-null).  Otherwise, the renaming is successful, and true
   is returned. */
bool
dict_rename_vars (struct dictionary *d,
                  struct variable **vars, char **new_names,
                  size_t count, char **err_name) 
{
  char **old_names;
  size_t i;
  bool success = true;

  assert (d != NULL);
  assert (count == 0 || vars != NULL);
  assert (count == 0 || new_names != NULL);

  /* Remove the variables to be renamed from the name hash,
     save their names, and rename them. */
  old_names = xnmalloc (count, sizeof *old_names);
  for (i = 0; i < count; i++) 
    {
      assert (d->var[vars[i]->index] == vars[i]);
      assert (var_is_plausible_name (new_names[i], false));
      hsh_force_delete (d->name_tab, vars[i]);
      old_names[i] = xstrdup (vars[i]->name);
      strcpy (vars[i]->name, new_names[i]);
    }

  /* Add the renamed variables back into the name hash,
     checking for conflicts. */
  for (i = 0; i < count; i++)
    {
      assert (new_names[i] != NULL);
      assert (*new_names[i] != '\0');
      assert (strlen (new_names[i]) >= 1);
      assert (strlen (new_names[i]) <= LONG_NAME_LEN);

      if (hsh_insert (d->name_tab, vars[i]) != NULL)
        {
          /* There is a name conflict.
             Back out all the name changes that have already
             taken place, and indicate failure. */
          size_t fail_idx = i;
          if (err_name != NULL) 
            *err_name = new_names[i];

          for (i = 0; i < fail_idx; i++)
            hsh_force_delete (d->name_tab, vars[i]);
          
          for (i = 0; i < count; i++)
            {
              strcpy (vars[i]->name, old_names[i]);
              hsh_force_insert (d->name_tab, vars[i]);
            }

          success = false;
          goto done;
        }
    }

  /* Clear short names. */
  if (get_algorithm () == ENHANCED)
    for (i = 0; i < count; i++)
      var_clear_short_name (vars[i]);

 done:
  /* Free the old names we kept around. */
  for (i = 0; i < count; i++)
    free (old_names[i]);
  free (old_names);

  return success;
}

/* Returns the weighting variable in dictionary D, or a null
   pointer if the dictionary is unweighted. */
struct variable *
dict_get_weight (const struct dictionary *d) 
{
  assert (d != NULL);
  assert (d->weight == NULL || dict_contains_var (d, d->weight));
  
  return d->weight;
}

/* Returns the value of D's weighting variable in case C, except that a
   negative weight is returned as 0.  Returns 1 if the dictionary is
   unweighted. Will warn about missing, negative, or zero values if
   warn_on_invalid is nonzero. The function will set warn_on_invalid to zero
   if an invalid weight is found. */
double
dict_get_case_weight (const struct dictionary *d, const struct ccase *c, 
		      int *warn_on_invalid)
{
  assert (d != NULL);
  assert (c != NULL);

  if (d->weight == NULL)
    return 1.0;
  else 
    {
      double w = case_num (c, d->weight->fv);
      if (w < 0.0 || mv_is_num_missing (&d->weight->miss, w))
        w = 0.0;
      if ( w == 0.0 && *warn_on_invalid ) {
	  *warn_on_invalid = 0;
	  msg (SW, _("At least one case in the data file had a weight value "
		     "that was user-missing, system-missing, zero, or "
		     "negative.  These case(s) were ignored."));
      }
      return w;
    }
}

/* Sets the weighting variable of D to V, or turning off
   weighting if V is a null pointer. */
void
dict_set_weight (struct dictionary *d, struct variable *v) 
{
  assert (d != NULL);
  assert (v == NULL || dict_contains_var (d, v));
  assert (v == NULL || v->type == NUMERIC);

  d->weight = v;
}

/* Returns the filter variable in dictionary D (see cmd_filter())
   or a null pointer if the dictionary is unfiltered. */
struct variable *
dict_get_filter (const struct dictionary *d) 
{
  assert (d != NULL);
  assert (d->filter == NULL || dict_contains_var (d, d->filter));
  
  return d->filter;
}

/* Sets V as the filter variable for dictionary D.  Passing a
   null pointer for V turn off filtering. */
void
dict_set_filter (struct dictionary *d, struct variable *v)
{
  assert (d != NULL);
  assert (v == NULL || dict_contains_var (d, v));

  d->filter = v;
}

/* Returns the case limit for dictionary D, or zero if the number
   of cases is unlimited (see cmd_n()). */
int
dict_get_case_limit (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->case_limit;
}

/* Sets CASE_LIMIT as the case limit for dictionary D.  Zero for
   CASE_LIMIT indicates no limit. */
void
dict_set_case_limit (struct dictionary *d, int case_limit) 
{
  assert (d != NULL);
  assert (case_limit >= 0);

  d->case_limit = case_limit;
}

/* Returns the index of the next value to be added to D.  This
   value is the number of `union value's that need to be
   allocated to store a case for dictionary D. */
int
dict_get_next_value_idx (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->next_value_idx;
}

/* Returns the number of bytes needed to store a case for
   dictionary D. */
size_t
dict_get_case_size (const struct dictionary *d) 
{
  assert (d != NULL);

  return sizeof (union value) * dict_get_next_value_idx (d);
}

/* Deletes scratch variables in dictionary D and reassigns values
   so that fragmentation is eliminated. */
void
dict_compact_values (struct dictionary *d) 
{
  size_t i;

  d->next_value_idx = 0;
  for (i = 0; i < d->var_cnt; )
    {
      struct variable *v = d->var[i];

      if (dict_class_from_id (v->name) != DC_SCRATCH) 
        {
          v->fv = d->next_value_idx;
          d->next_value_idx += v->nv;
          i++;
        }
      else
        dict_delete_var (d, v);
    }
}

/* Returns the number of values that would be used by a case if
   dict_compact_values() were called. */
size_t
dict_get_compacted_value_cnt (const struct dictionary *d) 
{
  size_t i;
  size_t cnt;

  cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    if (dict_class_from_id (d->var[i]->name) != DC_SCRATCH) 
      cnt += d->var[i]->nv;
  return cnt;
}

/* Creates and returns an array mapping from a dictionary index
   to the `fv' that the corresponding variable will have after
   calling dict_compact_values().  Scratch variables receive -1
   for `fv' because dict_compact_values() will delete them. */
int *
dict_get_compacted_idx_to_fv (const struct dictionary *d) 
{
  size_t i;
  size_t next_value_idx;
  int *idx_to_fv;
  
  idx_to_fv = xnmalloc (d->var_cnt, sizeof *idx_to_fv);
  next_value_idx = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];

      if (dict_class_from_id (v->name) != DC_SCRATCH) 
        {
          idx_to_fv[i] = next_value_idx;
          next_value_idx += v->nv;
        }
      else 
        idx_to_fv[i] = -1;
    }
  return idx_to_fv;
}

/* Returns true if a case for dictionary D would be smaller after
   compaction, false otherwise.  Compacting a case eliminates
   "holes" between values and after the last value.  Holes are
   created by deleting variables (or by scratch variables).

   The return value may differ from whether compacting a case
   from dictionary D would *change* the case: compaction could
   rearrange values even if it didn't reduce space
   requirements. */
bool
dict_needs_compaction (const struct dictionary *d) 
{
  return dict_get_compacted_value_cnt (d) < dict_get_next_value_idx (d);
}

/* How to copy a contiguous range of values between cases. */
struct copy_map
  {
    size_t src_idx;             /* Starting value index in source case. */
    size_t dst_idx;             /* Starting value index in target case. */
    size_t cnt;                 /* Number of values. */
  };

/* How to compact a case. */
struct dict_compactor 
  {
    struct copy_map *maps;      /* Array of mappings. */
    size_t map_cnt;             /* Number of mappings. */
  };

/* Creates and returns a dict_compactor that can be used to
   compact cases for dictionary D.

   Compacting a case eliminates "holes" between values and after
   the last value.  Holes are created by deleting variables (or
   by scratch variables). */
struct dict_compactor *
dict_make_compactor (const struct dictionary *d)
{
  struct dict_compactor *compactor;
  struct copy_map *map;
  size_t map_allocated;
  size_t value_idx;
  size_t i;

  compactor = xmalloc (sizeof *compactor);
  compactor->maps = NULL;
  compactor->map_cnt = 0;
  map_allocated = 0;

  value_idx = 0;
  map = NULL;
  for (i = 0; i < d->var_cnt; i++) 
    {
      struct variable *v = d->var[i];

      if (dict_class_from_id (v->name) == DC_SCRATCH)
        continue;
      if (map != NULL && map->src_idx + map->cnt == v->fv) 
        map->cnt += v->nv;
      else 
        {
          if (compactor->map_cnt == map_allocated)
            compactor->maps = x2nrealloc (compactor->maps, &map_allocated,
                                          sizeof *compactor->maps);
          map = &compactor->maps[compactor->map_cnt++];
          map->src_idx = v->fv;
          map->dst_idx = value_idx;
          map->cnt = v->nv;
        }
      value_idx += v->nv;
    }

  return compactor;
}

/* Compacts SRC by copying it to DST according to the scheme in
   COMPACTOR.

   Compacting a case eliminates "holes" between values and after
   the last value.  Holes are created by deleting variables (or
   by scratch variables). */
void
dict_compactor_compact (const struct dict_compactor *compactor,
                        struct ccase *dst, const struct ccase *src) 
{
  size_t i;

  for (i = 0; i < compactor->map_cnt; i++) 
    {
      const struct copy_map *map = &compactor->maps[i];
      case_copy (dst, map->dst_idx, src, map->src_idx, map->cnt);
    }
}

/* Destroys COMPACTOR. */
void
dict_compactor_destroy (struct dict_compactor *compactor) 
{
  if (compactor != NULL) 
    {
      free (compactor->maps);
      free (compactor);
    }
}

/* Returns the SPLIT FILE vars (see cmd_split_file()).  Call
   dict_get_split_cnt() to determine how many SPLIT FILE vars
   there are.  Returns a null pointer if and only if there are no
   SPLIT FILE vars. */
struct variable *const *
dict_get_split_vars (const struct dictionary *d) 
{
  assert (d != NULL);
  
  return d->split;
}

/* Returns the number of SPLIT FILE vars. */
size_t
dict_get_split_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->split_cnt;
}

/* Sets CNT split vars SPLIT in dictionary D. */
void
dict_set_split_vars (struct dictionary *d,
                     struct variable *const *split, size_t cnt)
{
  assert (d != NULL);
  assert (cnt == 0 || split != NULL);

  d->split_cnt = cnt;
  d->split = xnrealloc (d->split, cnt, sizeof *d->split);
  memcpy (d->split, split, cnt * sizeof *d->split);
}

/* Returns the file label for D, or a null pointer if D is
   unlabeled (see cmd_file_label()). */
const char *
dict_get_label (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->label;
}

/* Sets D's file label to LABEL, truncating it to a maximum of 60
   characters. */
void
dict_set_label (struct dictionary *d, const char *label) 
{
  assert (d != NULL);

  free (d->label);
  if (label == NULL)
    d->label = NULL;
  else if (strlen (label) < 60)
    d->label = xstrdup (label);
  else 
    {
      d->label = xmalloc (61);
      memcpy (d->label, label, 60);
      d->label[60] = '\0';
    }
}

/* Returns the documents for D, or a null pointer if D has no
   documents (see cmd_document()).. */
const char *
dict_get_documents (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->documents;
}

/* Sets the documents for D to DOCUMENTS, or removes D's
   documents if DOCUMENT is a null pointer. */
void
dict_set_documents (struct dictionary *d, const char *documents)
{
  assert (d != NULL);

  free (d->documents);
  if (documents == NULL)
    d->documents = NULL;
  else
    d->documents = xstrdup (documents);
}

/* Creates in D a vector named NAME that contains CNT variables
   VAR (see cmd_vector()).  Returns true if successful, or
   false if a vector named NAME already exists in D. */
bool
dict_create_vector (struct dictionary *d,
                    const char *name,
                    struct variable **var, size_t cnt) 
{
  struct vector *vector;
  size_t i;

  assert (d != NULL);
  assert (name != NULL);
  assert (var_is_plausible_name (name, false));
  assert (var != NULL);
  assert (cnt > 0);
  
  if (dict_lookup_vector (d, name) != NULL)
    return false;

  d->vector = xnrealloc (d->vector, d->vector_cnt + 1, sizeof *d->vector);
  vector = d->vector[d->vector_cnt] = xmalloc (sizeof *vector);
  vector->idx = d->vector_cnt++;
  str_copy_trunc (vector->name, sizeof vector->name, name);
  vector->var = xnmalloc (cnt, sizeof *var);
  for (i = 0; i < cnt; i++)
    {
      assert (dict_contains_var (d, var[i]));
      vector->var[i] = var[i];
    }
  vector->cnt = cnt;
  
  return true;
}

/* Returns the vector in D with index IDX, which must be less
   than dict_get_vector_cnt (D). */
const struct vector *
dict_get_vector (const struct dictionary *d, size_t idx) 
{
  assert (d != NULL);
  assert (idx < d->vector_cnt);

  return d->vector[idx];
}

/* Returns the number of vectors in D. */
size_t
dict_get_vector_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->vector_cnt;
}

/* Looks up and returns the vector within D with the given
   NAME. */
const struct vector *
dict_lookup_vector (const struct dictionary *d, const char *name) 
{
  size_t i;

  assert (d != NULL);
  assert (name != NULL);

  for (i = 0; i < d->vector_cnt; i++)
    if (!strcasecmp (d->vector[i]->name, name))
      return d->vector[i];
  return NULL;
}

/* Deletes all vectors from D. */
void
dict_clear_vectors (struct dictionary *d) 
{
  size_t i;
  
  assert (d != NULL);

  for (i = 0; i < d->vector_cnt; i++) 
    {
      free (d->vector[i]->var);
      free (d->vector[i]);
    }
  free (d->vector);
  d->vector = NULL;
  d->vector_cnt = 0;
}

/* Compares two strings. */
static int
compare_strings (const void *a, const void *b, void *aux UNUSED) 
{
  return strcmp (a, b);
}

/* Hashes a string. */
static unsigned
hash_string (const void *s, void *aux UNUSED) 
{
  return hsh_hash_string (s);
}

/* Assigns a valid, unique short_name[] to each variable in D.
   Each variable whose actual name is short has highest priority
   for that short name.  Otherwise, variables with an existing
   short_name[] have the next highest priority for a given short
   name; if it is already taken, then the variable is treated as
   if short_name[] had been empty.  Otherwise, long names are
   truncated to form short names.  If that causes conflicts,
   variables are renamed as PREFIX_A, PREFIX_B, and so on. */
void
dict_assign_short_names (struct dictionary *d) 
{
  struct hsh_table *short_names;
  size_t i;

  /* Give variables whose names are short the corresponding short
     names, and clear short_names[] that conflict with a variable
     name. */
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];
      if (strlen (v->name) <= SHORT_NAME_LEN)
        var_set_short_name (v, v->name);
      else if (dict_lookup_var (d, v->short_name) != NULL)
        var_clear_short_name (v);
    }

  /* Each variable with an assigned short_name[] now gets it
     unless there is a conflict. */
  short_names = hsh_create (d->var_cnt, compare_strings, hash_string,
                            NULL, NULL);
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];
      if (v->short_name[0] && hsh_insert (short_names, v->short_name) != NULL)
        var_clear_short_name (v);
    }
  
  /* Now assign short names to remaining variables. */
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];
      if (v->short_name[0] == '\0') 
        {
          int sfx;

          /* Form initial short_name. */
          var_set_short_name (v, v->name);

          /* Try _A, _B, ... _AA, _AB, etc., if needed. */
          for (sfx = 0; hsh_insert (short_names, v->short_name) != NULL; sfx++)
            var_set_short_name_suffix (v, v->name, sfx);
        } 
    }

  /* Get rid of hash table. */
  hsh_destroy (short_names);
}
