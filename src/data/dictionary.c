/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009 Free Software Foundation, Inc.

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

#include "dictionary.h"

#include <stdlib.h>
#include <ctype.h>

#include <data/attributes.h>
#include <data/case.h>
#include <data/category.h>
#include <data/identifier.h>
#include <data/settings.h>
#include <data/value-labels.h>
#include <data/vardict.h>
#include <data/variable.h>
#include <data/vector.h>
#include <libpspp/array.h>
#include <libpspp/assertion.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>

#include "intprops.h"
#include "minmax.h"
#include "xalloc.h"

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
    casenumber case_limit;      /* Current case limit (N command). */
    char *label;		/* File label. */
    struct string documents;    /* Documents, as a string. */
    struct vector **vector;     /* Vectors of variables. */
    size_t vector_cnt;          /* Number of vectors. */
    struct attrset attributes;  /* Custom attributes. */

    char *encoding;             /* Character encoding of string data */

    const struct dict_callbacks *callbacks; /* Callbacks on dictionary
					       modification */
    void *cb_data ;                  /* Data passed to callbacks */

    void (*changed) (struct dictionary *, void *); /* Generic change callback */
    void *changed_data;
  };


void
dict_set_encoding (struct dictionary *d, const char *enc)
{
  if (enc)
    d->encoding = xstrdup (enc);
}

const char *
dict_get_encoding (const struct dictionary *d)
{
  return d->encoding ;
}


void
dict_set_change_callback (struct dictionary *d,
			  void (*changed) (struct dictionary *, void*),
			  void *data)
{
  d->changed = changed;
  d->changed_data = data;
}


/* Print a representation of dictionary D to stdout, for
   debugging purposes. */
void
dict_dump (const struct dictionary *d)
{
  int i;
  for (i = 0 ; i < d->var_cnt ; ++i )
    {
      const struct variable *v =
	d->var[i];
      printf ("Name: %s;\tdict_idx: %d; case_idx: %d\n",
	      var_get_name (v),
	      var_get_dict_index (v),
	      var_get_case_index (v));

    }
}

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
  attrset_init (&d->attributes);
  return d;
}

/* Creates and returns a (deep) copy of an existing
   dictionary.

   The new dictionary's case indexes are copied from the old
   dictionary.  If the new dictionary won't be used to access
   cases produced with the old dictionary, then the new
   dictionary's case indexes should be compacted with
   dict_compact_values to save space. */
struct dictionary *
dict_clone (const struct dictionary *s)
{
  struct dictionary *d;
  size_t i;

  assert (s != NULL);

  d = dict_create ();

  for (i = 0; i < s->var_cnt; i++)
    {
      const struct vardict_info *svdi;
      struct vardict_info dvdi;
      struct variable *sv = s->var[i];
      struct variable *dv = dict_clone_var_assert (d, sv, var_get_name (sv));
      size_t i;

      for (i = 0; i < var_get_short_name_cnt (sv); i++)
        var_set_short_name (dv, i, var_get_short_name (sv, i));

      svdi = var_get_vardict (sv);
      dvdi = *svdi;
      dvdi.dict = d;
      var_set_vardict (dv, &dvdi);
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

  if ( s->encoding)
    d->encoding = xstrdup (s->encoding);

  dict_set_attributes (d, dict_get_attributes (s));

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
      dict_delete_var (d, d->var[d->var_cnt - 1]);
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
  ds_destroy (&d->documents);
  dict_clear_vectors (d);
  attrset_clear (&d->attributes);
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
      attrset_destroy (&d->attributes);
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

/* Sets *VARS to an array of pointers to variables in D and *CNT
   to the number of variables in *D.  All variables are returned
   except for those, if any, in the classes indicated by EXCLUDE.
   (There is no point in putting DC_SYSTEM in EXCLUDE as
   dictionaries never include system variables.) */
void
dict_get_vars (const struct dictionary *d, const struct variable ***vars,
               size_t *cnt, enum dict_class exclude)
{
  dict_get_vars_mutable (d, (struct variable ***) vars, cnt, exclude);
}

/* Sets *VARS to an array of pointers to variables in D and *CNT
   to the number of variables in *D.  All variables are returned
   except for those, if any, in the classes indicated by EXCLUDE.
   (There is no point in putting DC_SYSTEM in EXCLUDE as
   dictionaries never include system variables.) */
void
dict_get_vars_mutable (const struct dictionary *d, struct variable ***vars,
                       size_t *cnt, enum dict_class exclude)
{
  size_t count;
  size_t i;

  assert (d != NULL);
  assert (vars != NULL);
  assert (cnt != NULL);
  assert (exclude == (exclude & DC_ALL));

  count = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      enum dict_class class = var_get_dict_class (d->var[i]);
      if (!(class & exclude))
        count++;
    }

  *vars = xnmalloc (count, sizeof **vars);
  *cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      enum dict_class class = var_get_dict_class (d->var[i]);
      if (!(class & exclude))
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

  if ( d->changed ) d->changed (d, d->changed_data);
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

  if ( ! var_is_plausible_name (name, false))
    return NULL;

  target = var_create (name, 0);
  result = hsh_find (d->name_tab, target);
  var_destroy (target);

  if ( result && var_has_vardict (result)) 
  {
      const struct vardict_info *vdi = var_get_vardict (result);
      assert (vdi->dict == d);
  }

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

  if ( d->changed ) d->changed (d, d->changed_data);
  if ( d->callbacks &&  d->callbacks->var_changed )
    d->callbacks->var_changed (d, dict_index, d->cb_data);
}

/* Sets the case_index in V's vardict to CASE_INDEX. */
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
   been executed, as done by DELETE VARIABLES.

   Pointers to V within D are not a problem, because
   dict_delete_var() knows to remove V from split variables,
   weights, filters, etc. */
void
dict_delete_var (struct dictionary *d, struct variable *v)
{
  int dict_index = var_get_dict_index (v);
  const int case_index = var_get_case_index (v);
  const int val_cnt = var_get_value_cnt (v);

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

  if ( d->changed ) d->changed (d, d->changed_data);
  if (d->callbacks &&  d->callbacks->var_deleted )
    d->callbacks->var_deleted (d, dict_index, case_index, val_cnt, d->cb_data);
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
    if (var_get_dict_class (d->var[i]) == DC_SCRATCH)
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

  if (settings_get_algorithm () == ENHANCED)
    var_clear_short_names (v);

  if ( d->changed ) d->changed (d, d->changed_data);
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
  if (settings_get_algorithm () == ENHANCED)
    for (i = 0; i < count; i++)
      var_clear_short_names (vars[i]);

  pool_destroy (pool);
  return true;
}

/* Returns true if a variable named NAME may be inserted in DICT;
   that is, if there is not already a variable with that name in
   DICT and if NAME is not a reserved word.  (The caller's checks
   have already verified that NAME is otherwise acceptable as a
   variable name.) */
static bool
var_name_is_insertable (const struct dictionary *dict, const char *name)
{
  return (dict_lookup_var (dict, name) == NULL
          && lex_id_to_token (ss_cstr (name)) == T_ID);
}

static bool
make_hinted_name (const struct dictionary *dict, const char *hint,
                  char name[VAR_NAME_LEN + 1])
{
  bool dropped = false;
  char *cp;

  for (cp = name; *hint && cp < name + VAR_NAME_LEN; hint++)
    {
      if (cp == name
          ? lex_is_id1 (*hint) && *hint != '$'
          : lex_is_idn (*hint))
        {
          if (dropped)
            {
              *cp++ = '_';
              dropped = false;
            }
          if (cp < name + VAR_NAME_LEN)
            *cp++ = *hint;
        }
      else if (cp > name)
        dropped = true;
    }
  *cp = '\0';

  if (name[0] != '\0')
    {
      size_t len = strlen (name);
      unsigned long int i;

      if (var_name_is_insertable (dict, name))
        return true;

      for (i = 0; i < ULONG_MAX; i++)
        {
          char suffix[INT_BUFSIZE_BOUND (i) + 1];
          int ofs;

          suffix[0] = '_';
          if (!str_format_26adic (i + 1, &suffix[1], sizeof suffix - 1))
            NOT_REACHED ();

          ofs = MIN (VAR_NAME_LEN - strlen (suffix), len);
          strcpy (&name[ofs], suffix);

          if (var_name_is_insertable (dict, name))
            return true;
        }
    }

  return false;
}

static bool
make_numeric_name (const struct dictionary *dict, unsigned long int *num_start,
                   char name[VAR_NAME_LEN + 1])
{
  unsigned long int number;

  for (number = num_start != NULL ? MAX (*num_start, 1) : 1;
       number < ULONG_MAX;
       number++)
    {
      sprintf (name, "VAR%03lu", number);
      if (dict_lookup_var (dict, name) == NULL)
        {
          if (num_start != NULL)
            *num_start = number + 1;
          return true;
        }
    }

  if (num_start != NULL)
    *num_start = ULONG_MAX;
  return false;
}


/* Attempts to devise a variable name unique within DICT.
   Returns true if successful, in which case the new variable
   name is stored into NAME.  Returns false if all names that can
   be generated have already been taken.  (Returning false is
   quite unlikely: at least ULONG_MAX unique names can be
   generated.)

   HINT, if it is non-null, is used as a suggestion that will be
   modified for suitability as a variable name and for
   uniqueness.

   If HINT is null or entirely unsuitable, a name in the form
   "VAR%03d" will be generated, where the smallest unused integer
   value is used.  If NUM_START is non-null, then its value is
   used as the minimum numeric value to check, and it is updated
   to the next value to be checked.
   */
bool
dict_make_unique_var_name (const struct dictionary *dict, const char *hint,
                           unsigned long int *num_start,
                           char name[VAR_NAME_LEN + 1])
{
  return ((hint != NULL && make_hinted_name (dict, hint, name))
          || make_numeric_name (dict, num_start, name));
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

/* Returns the value of D's weighting variable in case C, except
   that a negative weight is returned as 0.  Returns 1 if the
   dictionary is unweighted.  Will warn about missing, negative,
   or zero values if *WARN_ON_INVALID is true.  The function will
   set *WARN_ON_INVALID to false if an invalid weight is
   found. */
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
      if ( w == 0.0 && warn_on_invalid != NULL && *warn_on_invalid ) {
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

  if (d->changed) d->changed (d, d->changed_data);
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
  assert (v == NULL || var_is_numeric (v));

  d->filter = v;

  if (d->changed) d->changed (d, d->changed_data);
  if ( d->callbacks && d->callbacks->filter_changed )
    d->callbacks->filter_changed (d,
				  v ? var_get_dict_index (v) : -1,
				  d->cb_data);
}

/* Returns the case limit for dictionary D, or zero if the number
   of cases is unlimited. */
casenumber
dict_get_case_limit (const struct dictionary *d)
{
  assert (d != NULL);

  return d->case_limit;
}

/* Sets CASE_LIMIT as the case limit for dictionary D.  Use
   0 for CASE_LIMIT to indicate no limit. */
void
dict_set_case_limit (struct dictionary *d, casenumber case_limit)
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

/* Reassigns values in dictionary D so that fragmentation is
   eliminated. */
void
dict_compact_values (struct dictionary *d)
{
  size_t i;

  d->next_value_idx = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];
      set_var_case_index (v, d->next_value_idx);
      d->next_value_idx += var_get_value_cnt (v);
    }
}

/*
   Reassigns case indices for D, increasing each index above START by
   the value PADDING.
*/
static void
dict_pad_values (struct dictionary *d, int start, int padding)
{
  size_t i;

  if ( padding <= 0 ) 
	return;

  for (i = 0; i < d->var_cnt; ++i)
    {
      struct variable *v = d->var[i];

      int index = var_get_case_index (v);

      if ( index >= start)
	set_var_case_index (v, index + padding);
    }

  d->next_value_idx += padding;
}


/* Returns the number of values occupied by the variables in
   dictionary D.  All variables are considered if EXCLUDE_CLASSES
   is 0, or it may contain one or more of (1u << DC_ORDINARY),
   (1u << DC_SYSTEM), or (1u << DC_SCRATCH) to exclude the
   corresponding type of variable.

   The return value may be less than the number of values in one
   of dictionary D's cases (as returned by
   dict_get_next_value_idx) even if E is 0, because there may be
   gaps in D's cases due to deleted variables. */
size_t
dict_count_values (const struct dictionary *d, unsigned int exclude_classes)
{
  size_t i;
  size_t cnt;

  assert ((exclude_classes & ~((1u << DC_ORDINARY)
                               | (1u << DC_SYSTEM)
                               | (1u << DC_SCRATCH))) == 0);

  cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      enum dict_class class = var_get_dict_class (d->var[i]);
      if (!(exclude_classes & (1u << class)))
        cnt += var_get_value_cnt (d->var[i]);
    }
  return cnt;
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

/* Removes variable V, which must be in D, from D's set of split
   variables. */
void
dict_unset_split_var (struct dictionary *d, struct variable *v)
{
  int orig_count;

  assert (dict_contains_var (d, v));

  orig_count = d->split_cnt;
  d->split_cnt = remove_equal (d->split, d->split_cnt, sizeof *d->split,
                               &v, compare_var_ptrs, NULL);
  if (orig_count != d->split_cnt)
    {
      if (d->changed) d->changed (d, d->changed_data);
      /* We changed the set of split variables so invoke the
         callback. */
      if (d->callbacks &&  d->callbacks->split_changed)
        d->callbacks->split_changed (d, d->cb_data);
    }
}

/* Sets CNT split vars SPLIT in dictionary D. */
void
dict_set_split_vars (struct dictionary *d,
                     struct variable *const *split, size_t cnt)
{
  assert (d != NULL);
  assert (cnt == 0 || split != NULL);

  d->split_cnt = cnt;
  if ( cnt > 0 )
   {
    d->split = xnrealloc (d->split, cnt, sizeof *d->split) ;
    memcpy (d->split, split, cnt * sizeof *d->split);
   }
  else
   {
    free (d->split);
    d->split = NULL;
   }

  if (d->changed) d->changed (d, d->changed_data);
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
  d->label = label != NULL ? xstrndup (label, 60) : NULL;
}

/* Returns the documents for D, or a null pointer if D has no
   documents.  If the return value is nonnull, then the string
   will be an exact multiple of DOC_LINE_LENGTH bytes in length,
   with each segment corresponding to one line. */
const char *
dict_get_documents (const struct dictionary *d)
{
  return ds_is_empty (&d->documents) ? NULL : ds_cstr (&d->documents);
}

/* Sets the documents for D to DOCUMENTS, or removes D's
   documents if DOCUMENT is a null pointer.  If DOCUMENTS is
   nonnull, then it should be an exact multiple of
   DOC_LINE_LENGTH bytes in length, with each segment
   corresponding to one line. */
void
dict_set_documents (struct dictionary *d, const char *documents)
{
  size_t remainder;

  ds_assign_cstr (&d->documents, documents != NULL ? documents : "");

  /* In case the caller didn't get it quite right, pad out the
     final line with spaces. */
  remainder = ds_length (&d->documents) % DOC_LINE_LENGTH;
  if (remainder != 0)
    ds_put_char_multiple (&d->documents, ' ', DOC_LINE_LENGTH - remainder);
}

/* Drops the documents from dictionary D. */
void
dict_clear_documents (struct dictionary *d)
{
  ds_clear (&d->documents);
}

/* Appends LINE to the documents in D.  LINE will be truncated or
   padded on the right with spaces to make it exactly
   DOC_LINE_LENGTH bytes long. */
void
dict_add_document_line (struct dictionary *d, const char *line)
{
  if (strlen (line) > DOC_LINE_LENGTH)
    {
      /* Note to translators: "bytes" is correct, not characters */
      msg (SW, _("Truncating document line to %d bytes."), DOC_LINE_LENGTH);
    }
  buf_copy_str_rpad (ds_put_uninit (&d->documents, DOC_LINE_LENGTH),
                     DOC_LINE_LENGTH, line);
}

/* Returns the number of document lines in dictionary D. */
size_t
dict_get_document_line_cnt (const struct dictionary *d)
{
  return ds_length (&d->documents) / DOC_LINE_LENGTH;
}

/* Copies document line number IDX from dictionary D into
   LINE, trimming off any trailing white space. */
void
dict_get_document_line (const struct dictionary *d,
                        size_t idx, struct string *line)
{
  assert (idx < dict_get_document_line_cnt (d));
  ds_assign_substring (line, ds_substr (&d->documents, idx * DOC_LINE_LENGTH,
                                        DOC_LINE_LENGTH));
  ds_rtrim (line, ss_cstr (CC_SPACES));
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

/* Returns D's attribute set.  The caller may examine or modify
   the attribute set, but must not destroy it.  Destroying D or
   calling dict_set_attributes for D will also destroy D's
   attribute set. */
struct attrset *
dict_get_attributes (const struct dictionary *d) 
{
  return (struct attrset *) &d->attributes;
}

/* Replaces D's attributes set by a copy of ATTRS. */
void
dict_set_attributes (struct dictionary *d, const struct attrset *attrs)
{
  attrset_destroy (&d->attributes);
  attrset_clone (&d->attributes, attrs);
}

/* Returns true if D has at least one attribute in its attribute
   set, false if D's attribute set is empty. */
bool
dict_has_attributes (const struct dictionary *d) 
{
  return attrset_count (&d->attributes) > 0;
}

/* Called from variable.c to notify the dictionary that some property of
   the variable has changed */
void
dict_var_changed (const struct variable *v)
{
  if ( var_has_vardict (v))
    {
      const struct vardict_info *vdi = var_get_vardict (v);
      struct dictionary *d = vdi->dict;

      if ( NULL == d)
	return;

      if (d->changed ) d->changed (d, d->changed_data);
      if ( d->callbacks && d->callbacks->var_changed )
	d->callbacks->var_changed (d, var_get_dict_index (v), d->cb_data);
    }
}


/* Called from variable.c to notify the dictionary that the variable's width
   has changed */
void
dict_var_resized (const struct variable *v, int delta)
{
  if ( var_has_vardict (v))
    {
      const struct vardict_info *vdi = var_get_vardict (v);
      struct dictionary *d;

      d = vdi->dict;

      dict_pad_values (d, var_get_case_index(v) + 1, delta);

      if (d->changed) d->changed (d, d->changed_data);
      if ( d->callbacks && d->callbacks->var_resized )
	d->callbacks->var_resized (d, var_get_dict_index (v), delta, d->cb_data);
    }
}

/* Called from variable.c to notify the dictionary that the variable's display width
   has changed */
void
dict_var_display_width_changed (const struct variable *v)
{
  if ( var_has_vardict (v))
    {
      const struct vardict_info *vdi = var_get_vardict (v);
      struct dictionary *d;

      d = vdi->dict;

      if (d->changed) d->changed (d, d->changed_data);
      if ( d->callbacks && d->callbacks->var_display_width_changed )
	d->callbacks->var_display_width_changed (d, var_get_dict_index (v), d->cb_data);
    }
}

