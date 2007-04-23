/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006, 2007 Free Software Foundation, Inc.

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

#include "case.h"
#include "category.h"
#include "settings.h"
#include "value-labels.h"
#include "vardict.h"
#include "variable.h"
#include "vector.h"
#include <libpspp/alloc.h>
#include <libpspp/array.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>

#include "minmax.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A dictionary. */
struct dictionary
  {
    struct variable **var;	/* Variables. */
    size_t var_cnt, var_cap;    /* Number of variables, capacity. */
    struct hsh_table *name_tab;	/* Variable index by name. */
    int next_value_idx;         /* Index of next `union value' to allocate. */
    const struct variable **split;    /* SPLIT FILE vars. */
    size_t split_cnt;           /* SPLIT FILE count. */
    struct variable *weight;    /* WEIGHT variable. */
    struct variable *filter;    /* FILTER variable. */
    size_t case_limit;          /* Current case limit (N command). */
    char *label;		/* File label. */
    char *documents;		/* Documents, as a string. */
    struct vector **vector;     /* Vectors of variables. */
    size_t vector_cnt;          /* Number of vectors. */
    const struct dict_callbacks *callbacks; /* Callbacks on dictionary
					       modification */
    void *cb_data ;                  /* Data passed to callbacks */
  };


/* Associate CALLBACKS with DICT.  Callbacks will be invoked whenever
   the dictionary or any of the variables it contains are modified.
   Each callback will get passed CALLBACK_DATA.
   Any callback may be NULL, in which case it'll be ignored.
*/
void
dict_set_callbacks (struct dictionary *dict,
		    const struct dict_callbacks *callbacks,
		    void *callback_data)
{
  dict->callbacks = callbacks;
  dict->cb_data = callback_data;
}

/* Shallow copy the callbacks from SRC to DEST */
void
dict_copy_callbacks (struct dictionary *dest,
		     const struct dictionary *src)
{
  dest->callbacks = src->callbacks;
  dest->cb_data = src->cb_data;
}

/* Creates and returns a new dictionary. */
struct dictionary *
dict_create (void)
{
  struct dictionary *d = xzalloc (sizeof *d);

  d->name_tab = hsh_create (8, compare_vars_by_name, hash_var_by_name,
                            NULL, NULL);
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
      struct variable *dv = dict_clone_var_assert (d, sv, var_get_name (sv));
      var_set_short_name (dv, var_get_short_name (sv));
    }

  d->next_value_idx = s->next_value_idx;

  d->split_cnt = s->split_cnt;
  if (d->split_cnt > 0)
    {
      d->split = xnmalloc (d->split_cnt, sizeof *d->split);
      for (i = 0; i < d->split_cnt; i++)
        d->split[i] = dict_lookup_var_assert (d, var_get_name (s->split[i]));
    }

  if (s->weight != NULL)
    dict_set_weight (d, dict_lookup_var_assert (d, var_get_name (s->weight)));

  if (s->filter != NULL)
    dict_set_filter (d, dict_lookup_var_assert (d, var_get_name (s->filter)));

  d->case_limit = s->case_limit;
  dict_set_label (d, dict_get_label (s));
  dict_set_documents (d, dict_get_documents (s));

  d->vector_cnt = s->vector_cnt;
  d->vector = xnmalloc (d->vector_cnt, sizeof *d->vector);
  for (i = 0; i < s->vector_cnt; i++)
    d->vector[i] = vector_clone (s->vector[i], s, d);

  return d;
}

/* Clears the contents from a dictionary without destroying the
   dictionary itself. */
void
dict_clear (struct dictionary *d)
{
  /* FIXME?  Should we really clear case_limit, label, documents?
     Others are necessarily cleared by deleting all the variables.*/
  assert (d != NULL);

  while (d->var_cnt > 0 )
    {
      var_clear_vardict (d->var[d->var_cnt - 1]);
      var_destroy (d->var[d->var_cnt -1]);

      d->var_cnt--;

      if (d->callbacks &&  d->callbacks->var_deleted )
	d->callbacks->var_deleted (d, d->var_cnt, d->cb_data);
    }
  free (d->var);
  d->var = NULL;
  d->var_cnt = d->var_cap = 0;
  hsh_clear (d->name_tab);
  d->next_value_idx = 0;
  dict_set_split_vars (d, NULL, 0);
  dict_set_weight (d, NULL);
  dict_set_filter (d, NULL);
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
      /* In general, we don't want callbacks occuring, if the dictionary
	 is being destroyed */
      d->callbacks  = NULL ;

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

/* Returns the variable in D with dictionary index IDX, which
   must be between 0 and the count returned by
   dict_get_var_cnt(), exclusive. */
struct variable *
dict_get_var (const struct dictionary *d, size_t idx)
{
  assert (d != NULL);
  assert (idx < d->var_cnt);

  return d->var[idx];
}

inline void
dict_get_vars (const struct dictionary *d, const struct variable ***vars,
               size_t *cnt, unsigned exclude_classes)
{
  dict_get_vars_mutable (d, (struct variable ***) vars, cnt, exclude_classes);
}

/* Sets *VARS to an array of pointers to variables in D and *CNT
   to the number of variables in *D.  All variables are returned
   if EXCLUDE_CLASSES is 0, or it may contain one or more of (1u
   << DC_ORDINARY), (1u << DC_SYSTEM), or (1u << DC_SCRATCH) to
   exclude the corresponding type of variable. */
void
dict_get_vars_mutable (const struct dictionary *d, struct variable ***vars,
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
    {
      enum dict_class class = dict_class_from_id (var_get_name (d->var[i]));
      if (!(exclude_classes & (1u << class)))
        count++;
    }

  *vars = xnmalloc (count, sizeof **vars);
  *cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      enum dict_class class = dict_class_from_id (var_get_name (d->var[i]));
      if (!(exclude_classes & (1u << class)))
        (*vars)[(*cnt)++] = d->var[i];
    }
  assert (*cnt == count);
}

static struct variable *
add_var (struct dictionary *d, struct variable *v)
{
  /* Add dictionary info to variable. */
  struct vardict_info vdi;
  vdi.case_index = d->next_value_idx;
  vdi.dict_index = d->var_cnt;
  vdi.dict = d;
  var_set_vardict (v, &vdi);

  /* Update dictionary. */
  if (d->var_cnt >= d->var_cap)
    {
      d->var_cap = 8 + 2 * d->var_cap;
      d->var = xnrealloc (d->var, d->var_cap, sizeof *d->var);
    }
  d->var[d->var_cnt++] = v;
  hsh_force_insert (d->name_tab, v);

  if ( d->callbacks &&  d->callbacks->var_added )
    d->callbacks->var_added (d, var_get_dict_index (v), d->cb_data);

  d->next_value_idx += var_get_value_cnt (v);

  return v;
}

/* Creates and returns a new variable in D with the given NAME
   and WIDTH.  Returns a null pointer if the given NAME would
   duplicate that of an existing variable in the dictionary. */
struct variable *
dict_create_var (struct dictionary *d, const char *name, int width)
{
  return (dict_lookup_var (d, name) == NULL
          ? dict_create_var_assert (d, name, width)
          : NULL);
}

/* Creates and returns a new variable in D with the given NAME
   and WIDTH.  Assert-fails if the given NAME would duplicate
   that of an existing variable in the dictionary. */
struct variable *
dict_create_var_assert (struct dictionary *d, const char *name, int width)
{
  assert (dict_lookup_var (d, name) == NULL);
  return add_var (d, var_create (name, width));
}

/* Creates and returns a new variable in D with name NAME, as a
   copy of existing variable OLD_VAR, which need not be in D or
   in any dictionary.  Returns a null pointer if the given NAME
   would duplicate that of an existing variable in the
   dictionary. */
struct variable *
dict_clone_var (struct dictionary *d, const struct variable *old_var,
                const char *name)
{
  return (dict_lookup_var (d, name) == NULL
          ? dict_clone_var_assert (d, old_var, name)
          : NULL);
}

/* Creates and returns a new variable in D with name NAME, as a
   copy of existing variable OLD_VAR, which need not be in D or
   in any dictionary.  Assert-fails if the given NAME would
   duplicate that of an existing variable in the dictionary. */
struct variable *
dict_clone_var_assert (struct dictionary *d, const struct variable *old_var,
                       const char *name)
{
  struct variable *new_var = var_clone (old_var);
  assert (dict_lookup_var (d, name) == NULL);
  var_set_name (new_var, name);
  return add_var (d, new_var);
}

/* Returns the variable named NAME in D, or a null pointer if no
   variable has that name. */
struct variable *
dict_lookup_var (const struct dictionary *d, const char *name)
{
  struct variable *target ;
  struct variable *result ;

  if ( ! var_is_valid_name (name, false))
    return NULL;

  target = var_create (name, 0);
  result = hsh_find (d->name_tab, target);
  var_destroy (target);

  return result;
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
  if (var_has_vardict (v))
    {
      const struct vardict_info *vdi = var_get_vardict (v);
      return (vdi->dict_index >= 0
              && vdi->dict_index < d->var_cnt
              && d->var[vdi->dict_index] == v);
    }
  else
    return false;
}

/* Compares two double pointers to variables, which should point
   to elements of a struct dictionary's `var' member array. */
static int
compare_var_ptrs (const void *a_, const void *b_, const void *aux UNUSED)
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  return *a < *b ? -1 : *a > *b;
}

/* Sets the dict_index in V's vardict to DICT_INDEX. */
static void
set_var_dict_index (struct variable *v, int dict_index)
{
  struct vardict_info vdi = *var_get_vardict (v);
  struct dictionary *d = vdi.dict;
  vdi.dict_index = dict_index;
  var_set_vardict (v, &vdi);

  if ( d->callbacks &&  d->callbacks->var_changed )
    d->callbacks->var_changed (d, dict_index, d->cb_data);
}

/* Sets the case_index in V's vardict to DICT_INDEX. */
static void
set_var_case_index (struct variable *v, int case_index)
{
  struct vardict_info vdi = *var_get_vardict (v);
  vdi.case_index = case_index;
  var_set_vardict (v, &vdi);
}

/* Re-sets the dict_index in the dictionary variables with
   indexes from FROM to TO (exclusive). */
static void
reindex_vars (struct dictionary *d, size_t from, size_t to)
{
  size_t i;

  for (i = from; i < to; i++)
    set_var_dict_index (d->var[i], i);
}

/* Deletes variable V from dictionary D and frees V.

   This is a very bad idea if there might be any pointers to V
   from outside D.  In general, no variable in the active file's
   dictionary should be deleted when any transformations are
   active on the dictionary's dataset, because those
   transformations might reference the deleted variable.  The
   safest time to delete a variable is just after a procedure has
   been executed, as done by MODIFY VARS.

   Pointers to V within D are not a problem, because
   dict_delete_var() knows to remove V from split variables,
   weights, filters, etc. */
void
dict_delete_var (struct dictionary *d, struct variable *v)
{
  int dict_index = var_get_dict_index (v);

  assert (dict_contains_var (d, v));

  /* Delete aux data. */
  var_clear_aux (v);

  dict_unset_split_var (d, v);

  if (d->weight == v)
    dict_set_weight (d, NULL);

  if (d->filter == v)
    dict_set_filter (d, NULL);

  dict_clear_vectors (d);

  /* Remove V from var array. */
  remove_element (d->var, d->var_cnt, sizeof *d->var, dict_index);
  d->var_cnt--;

  /* Update dict_index for each affected variable. */
  reindex_vars (d, dict_index, d->var_cnt);

  /* Update name hash. */
  hsh_force_delete (d->name_tab, v);


  /* Free memory. */
  var_clear_vardict (v);
  var_destroy (v);

  if (d->callbacks &&  d->callbacks->var_deleted )
    d->callbacks->var_deleted (d, dict_index, d->cb_data);
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

/* Deletes the COUNT variables in D starting at index IDX.  This
   is unsafe; see the comment on dict_delete_var() for
   details. */
void
dict_delete_consecutive_vars (struct dictionary *d, size_t idx, size_t count)
{
  /* FIXME: this can be done in O(count) time, but this algorithm
     is O(count**2). */
  assert (idx + count <= d->var_cnt);

  while (count-- > 0)
    dict_delete_var (d, d->var[idx]);
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
    if (dict_class_from_id (var_get_name (d->var[i])) == DC_SCRATCH)
      dict_delete_var (d, d->var[i]);
    else
      i++;
}

/* Moves V to 0-based position IDX in D.  Other variables in D,
   if any, retain their relative positions.  Runs in time linear
   in the distance moved. */
void
dict_reorder_var (struct dictionary *d, struct variable *v, size_t new_index)
{
  size_t old_index = var_get_dict_index (v);

  assert (new_index < d->var_cnt);
  move_element (d->var, d->var_cnt, sizeof *d->var, old_index, new_index);
  reindex_vars (d, MIN (old_index, new_index), MAX (old_index, new_index) + 1);
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
      size_t index = var_get_dict_index (order[i]);
      assert (d->var[index] == order[i]);
      d->var[index] = NULL;
      set_var_dict_index (order[i], i);
    }
  for (i = 0; i < d->var_cnt; i++)
    if (d->var[i] != NULL)
      {
        assert (count < d->var_cnt);
        new_var[count] = d->var[i];
        set_var_dict_index (new_var[count], count);
        count++;
      }
  free (d->var);
  d->var = new_var;
}

/* Changes the name of variable V in dictionary D to NEW_NAME. */
static void
rename_var (struct dictionary *d, struct variable *v, const char *new_name)
{
  struct vardict_info vdi;

  assert (dict_contains_var (d, v));

  vdi = *var_get_vardict (v);
  var_clear_vardict (v);
  var_set_name (v, new_name);
  var_set_vardict (v, &vdi);
}

/* Changes the name of V in D to name NEW_NAME.  Assert-fails if
   a variable named NEW_NAME is already in D, except that
   NEW_NAME may be the same as V's existing name. */
void
dict_rename_var (struct dictionary *d, struct variable *v,
                 const char *new_name)
{
  assert (!strcasecmp (var_get_name (v), new_name)
          || dict_lookup_var (d, new_name) == NULL);

  hsh_force_delete (d->name_tab, v);
  rename_var (d, v, new_name);
  hsh_force_insert (d->name_tab, v);

  if (get_algorithm () == ENHANCED)
    var_clear_short_name (v);

  if ( d->callbacks &&  d->callbacks->var_changed )
    d->callbacks->var_changed (d, var_get_dict_index (v), d->cb_data);
}

/* Renames COUNT variables specified in VARS to the names given
   in NEW_NAMES within dictionary D.  If the renaming would
   result in a duplicate variable name, returns false and stores a
   name that would be duplicated into *ERR_NAME (if ERR_NAME is
   non-null).  Otherwise, the renaming is successful, and true
   is returned. */
bool
dict_rename_vars (struct dictionary *d,
                  struct variable **vars, char **new_names, size_t count,
                  char **err_name)
{
  struct pool *pool;
  char **old_names;
  size_t i;

  assert (count == 0 || vars != NULL);
  assert (count == 0 || new_names != NULL);

  /* Save the names of the variables to be renamed. */
  pool = pool_create ();
  old_names = pool_nalloc (pool, count, sizeof *old_names);
  for (i = 0; i < count; i++)
    old_names[i] = pool_strdup (pool, var_get_name (vars[i]));

  /* Remove the variables to be renamed from the name hash,
     and rename them. */
  for (i = 0; i < count; i++)
    {
      hsh_force_delete (d->name_tab, vars[i]);
      rename_var (d, vars[i], new_names[i]);
    }

  /* Add the renamed variables back into the name hash,
     checking for conflicts. */
  for (i = 0; i < count; i++)
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
            rename_var (d, vars[i], old_names[i]);
            hsh_force_insert (d->name_tab, vars[i]);
          }

        pool_destroy (pool);
        return false;
      }

  /* Clear short names. */
  if (get_algorithm () == ENHANCED)
    for (i = 0; i < count; i++)
      var_clear_short_name (vars[i]);

  pool_destroy (pool);
  return true;
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
   warn_on_invalid is true. The function will set warn_on_invalid to false
   if an invalid weight is found. */
double
dict_get_case_weight (const struct dictionary *d, const struct ccase *c,
		      bool *warn_on_invalid)
{
  assert (d != NULL);
  assert (c != NULL);

  if (d->weight == NULL)
    return 1.0;
  else
    {
      double w = case_num (c, d->weight);
      if (w < 0.0 || var_is_num_missing (d->weight, w, MV_ANY))
        w = 0.0;
      if ( w == 0.0 && *warn_on_invalid ) {
	  *warn_on_invalid = false;
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
  assert (v == NULL || var_is_numeric (v));

  d->weight = v;

  if ( d->callbacks &&  d->callbacks->weight_changed )
    d->callbacks->weight_changed (d,
				  v ? var_get_dict_index (v) : -1,
				  d->cb_data);
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

  if ( d->callbacks && d->callbacks->filter_changed )
    d->callbacks->filter_changed (d,
				  v ? var_get_dict_index (v) : -1,
				  d->cb_data);
}

/* Returns the case limit for dictionary D, or zero if the number
   of cases is unlimited. */
size_t
dict_get_case_limit (const struct dictionary *d)
{
  assert (d != NULL);

  return d->case_limit;
}

/* Sets CASE_LIMIT as the case limit for dictionary D.  Use
   0 for CASE_LIMIT to indicate no limit. */
void
dict_set_case_limit (struct dictionary *d, size_t case_limit)
{
  assert (d != NULL);

  d->case_limit = case_limit;
}

/* Returns the case index of the next value to be added to D.
   This value is the number of `union value's that need to be
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

      if (dict_class_from_id (var_get_name (v)) != DC_SCRATCH)
        {
          set_var_case_index (v, d->next_value_idx);
          d->next_value_idx += var_get_value_cnt (v);
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
    if (dict_class_from_id (var_get_name (d->var[i])) != DC_SCRATCH)
      cnt += var_get_value_cnt (d->var[i]);
  return cnt;
}

/* Creates and returns an array mapping from a dictionary index
   to the case index that the corresponding variable will have
   after calling dict_compact_values().  Scratch variables
   receive -1 for case index because dict_compact_values() will
   delete them. */
int *
dict_get_compacted_dict_index_to_case_index (const struct dictionary *d)
{
  size_t i;
  size_t next_value_idx;
  int *map;

  map = xnmalloc (d->var_cnt, sizeof *map);
  next_value_idx = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];

      if (dict_class_from_id (var_get_name (v)) != DC_SCRATCH)
        {
          map[i] = next_value_idx;
          next_value_idx += var_get_value_cnt (v);
        }
      else
        map[i] = -1;
    }
  return map;
}

/* Returns true if a case for dictionary D would be smaller after
   compacting, false otherwise.  Compacting a case eliminates
   "holes" between values and after the last value.  Holes are
   created by deleting variables (or by scratch variables).

   The return value may differ from whether compacting a case
   from dictionary D would *change* the case: compacting could
   rearrange values even if it didn't reduce space
   requirements. */
bool
dict_compacting_would_shrink (const struct dictionary *d)
{
  return dict_get_compacted_value_cnt (d) < dict_get_next_value_idx (d);
}

/* Returns true if a case for dictionary D would change after
   compacting, false otherwise.  Compacting a case eliminates
   "holes" between values and after the last value.  Holes are
   created by deleting variables (or by scratch variables).

   The return value may differ from whether compacting a case
   from dictionary D would *shrink* the case: compacting could
   rearrange values without reducing space requirements. */
bool
dict_compacting_would_change (const struct dictionary *d)
{
  size_t case_idx;
  size_t i;

  case_idx = 0;
  for (i = 0; i < dict_get_var_cnt (d); i++)
    {
      struct variable *v = dict_get_var (d, i);
      if (var_get_case_index (v) != case_idx)
        return true;
      case_idx += var_get_value_cnt (v);
    }
  return false;
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

      if (dict_class_from_id (var_get_name (v)) == DC_SCRATCH)
        continue;
      if (map != NULL && map->src_idx + map->cnt == var_get_case_index (v))
        map->cnt += var_get_value_cnt (v);
      else
        {
          if (compactor->map_cnt == map_allocated)
            compactor->maps = x2nrealloc (compactor->maps, &map_allocated,
                                          sizeof *compactor->maps);
          map = &compactor->maps[compactor->map_cnt++];
          map->src_idx = var_get_case_index (v);
          map->dst_idx = value_idx;
          map->cnt = var_get_value_cnt (v);
        }
      value_idx += var_get_value_cnt (v);
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
const struct variable *const *
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

/* Removes variable V from the set of split variables in dictionary D */
void
dict_unset_split_var (struct dictionary *d,
		      struct variable *v)
{
  const int count = d->split_cnt;
  d->split_cnt = remove_equal (d->split, d->split_cnt, sizeof *d->split,
                               &v, compare_var_ptrs, NULL);

  if ( count == d->split_cnt)
    return;

  if ( d->callbacks &&  d->callbacks->split_changed )
    d->callbacks->split_changed (d, d->cb_data);
}

/* Sets CNT split vars SPLIT in dictionary D. */
void
dict_set_split_vars (struct dictionary *d,
                     struct variable *const *split, size_t cnt)
{
  assert (d != NULL);
  assert (cnt == 0 || split != NULL);

  d->split_cnt = cnt;
  d->split = cnt > 0 ? xnrealloc (d->split, cnt, sizeof *d->split) : NULL;
  memcpy (d->split, split, cnt * sizeof *d->split);

  if ( d->callbacks &&  d->callbacks->split_changed )
    d->callbacks->split_changed (d, d->cb_data);
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

/* Creates in D a vector named NAME that contains the CNT
   variables in VAR.  Returns true if successful, or false if a
   vector named NAME already exists in D. */
bool
dict_create_vector (struct dictionary *d,
                    const char *name,
                    struct variable **var, size_t cnt)
{
  size_t i;

  assert (var != NULL);
  assert (cnt > 0);
  for (i = 0; i < cnt; i++)
    assert (dict_contains_var (d, var[i]));

  if (dict_lookup_vector (d, name) == NULL)
    {
      d->vector = xnrealloc (d->vector, d->vector_cnt + 1, sizeof *d->vector);
      d->vector[d->vector_cnt++] = vector_create (name, var, cnt);
      return true;
    }
  else
    return false;
}

/* Creates in D a vector named NAME that contains the CNT
   variables in VAR.  A vector named NAME must not already exist
   in D. */
void
dict_create_vector_assert (struct dictionary *d,
                           const char *name,
                           struct variable **var, size_t cnt)
{
  assert (dict_lookup_vector (d, name) == NULL);
  dict_create_vector (d, name, var, cnt);
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
  for (i = 0; i < d->vector_cnt; i++)
    if (!strcasecmp (vector_get_name (d->vector[i]), name))
      return d->vector[i];
  return NULL;
}

/* Deletes all vectors from D. */
void
dict_clear_vectors (struct dictionary *d)
{
  size_t i;

  for (i = 0; i < d->vector_cnt; i++)
    vector_destroy (d->vector[i]);
  free (d->vector);

  d->vector = NULL;
  d->vector_cnt = 0;
}

/* Compares two strings. */
static int
compare_strings (const void *a, const void *b, const void *aux UNUSED)
{
  return strcmp (a, b);
}

/* Hashes a string. */
static unsigned
hash_string (const void *s, const void *aux UNUSED)
{
  return hsh_hash_string (s);
}


/* Sets V's short name to BASE, followed by a suffix of the form
   _A, _B, _C, ..., _AA, _AB, etc. according to the value of
   SUFFIX_NUMBER.  Truncates BASE as necessary to fit. */
static void
set_var_short_name_suffix (struct variable *v, const char *base,
                           int suffix_number)
{
  char suffix[SHORT_NAME_LEN + 1];
  char short_name[SHORT_NAME_LEN + 1];
  char *start, *end;
  int len, ofs;

  assert (v != NULL);
  assert (suffix_number >= 0);

  /* Set base name. */
  var_set_short_name (v, base);

  /* Compose suffix. */
  start = end = suffix + sizeof suffix - 1;
  *end = '\0';
  do
    {
      *--start = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[suffix_number % 26];
      if (start <= suffix + 1)
        msg (SE, _("Variable suffix too large."));
      suffix_number /= 26;
    }
  while (suffix_number > 0);
  *--start = '_';

  /* Append suffix to V's short name. */
  str_copy_trunc (short_name, sizeof short_name, base);
  len = end - start;
  if (len + strlen (short_name) > SHORT_NAME_LEN)
    ofs = SHORT_NAME_LEN - len;
  else
    ofs = strlen (short_name);
  strcpy (short_name + ofs, start);

  /* Set name. */
  var_set_short_name (v, short_name);
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
      const char *short_name = var_get_short_name (v);
      if (strlen (var_get_name (v)) <= SHORT_NAME_LEN)
        var_set_short_name (v, var_get_name (v));
      else if (short_name != NULL && dict_lookup_var (d, short_name) != NULL)
        var_clear_short_name (v);
    }

  /* Each variable with an assigned short_name[] now gets it
     unless there is a conflict. */
  short_names = hsh_create (d->var_cnt, compare_strings, hash_string,
                            NULL, NULL);
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];
      const char *name = var_get_short_name (v);
      if (name != NULL && hsh_insert (short_names, (char *) name) != NULL)
        var_clear_short_name (v);
    }

  /* Now assign short names to remaining variables. */
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];
      const char *name = var_get_short_name (v);
      if (name == NULL)
        {
          /* Form initial short_name from the variable name, then
             try _A, _B, ... _AA, _AB, etc., if needed.*/
          int trial = 0;
          do
            {
              if (trial == 0)
                var_set_short_name (v, var_get_name (v));
              else
                set_var_short_name_suffix (v, var_get_name (v), trial - 1);

              trial++;
            }
          while (hsh_insert (short_names, (char *) var_get_short_name (v))
                 != NULL);
        }
    }

  /* Get rid of hash table. */
  hsh_destroy (short_names);
}


/* Called from variable.c to notify the dictionary that some property of
   the variable has changed */
void
dict_var_changed (const struct variable *v)
{
  if ( var_has_vardict (v))
    {
      const struct vardict_info *vdi = var_get_vardict (v);
      struct dictionary *d;

      d = vdi->dict;

      if ( d->callbacks && d->callbacks->var_changed )
	d->callbacks->var_changed (d, var_get_dict_index (v), d->cb_data);
    }
}
