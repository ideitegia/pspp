/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include "data/dictionary.h"

#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistr.h>

#include "data/attributes.h"
#include "data/case.h"
#include "data/identifier.h"
#include "data/mrset.h"
#include "data/settings.h"
#include "data/value-labels.h"
#include "data/vardict.h"
#include "data/variable.h"
#include "data/vector.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"
#include "libpspp/string-array.h"

#include "gl/intprops.h"
#include "gl/minmax.h"
#include "gl/xalloc.h"
#include "gl/xmemdup0.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* A dictionary. */
struct dictionary
  {
    struct vardict_info *var;	/* Variables. */
    size_t var_cnt, var_cap;    /* Number of variables, capacity. */
    struct caseproto *proto;    /* Prototype for dictionary cases
                                   (updated lazily). */
    struct hmap name_map;	/* Variable index by name. */
    int next_value_idx;         /* Index of next `union value' to allocate. */
    const struct variable **split;    /* SPLIT FILE vars. */
    size_t split_cnt;           /* SPLIT FILE count. */
    struct variable *weight;    /* WEIGHT variable. */
    struct variable *filter;    /* FILTER variable. */
    casenumber case_limit;      /* Current case limit (N command). */
    char *label;		/* File label. */
    struct string_array documents; /* Documents. */
    struct vector **vector;     /* Vectors of variables. */
    size_t vector_cnt;          /* Number of vectors. */
    struct attrset attributes;  /* Custom attributes. */
    struct mrset **mrsets;      /* Multiple response sets. */
    size_t n_mrsets;            /* Number of multiple response sets. */

    char *encoding;             /* Character encoding of string data */

    const struct dict_callbacks *callbacks; /* Callbacks on dictionary
					       modification */
    void *cb_data ;                  /* Data passed to callbacks */

    void (*changed) (struct dictionary *, void *); /* Generic change callback */
    void *changed_data;
  };

static void dict_unset_split_var (struct dictionary *, struct variable *);
static void dict_unset_mrset_var (struct dictionary *, struct variable *);

/* Returns the encoding for data in dictionary D.  The return value is a
   nonnull string that contains an IANA character set name. */
const char *
dict_get_encoding (const struct dictionary *d)
{
  return d->encoding ;
}

/* Returns true if UTF-8 string ID is an acceptable identifier in DICT's
   encoding, false otherwise.  If ISSUE_ERROR is true, issues an explanatory
   error message on failure. */
bool
dict_id_is_valid (const struct dictionary *dict, const char *id,
                  bool issue_error)
{
  return id_is_valid (id, dict->encoding, issue_error);
}

void
dict_set_change_callback (struct dictionary *d,
			  void (*changed) (struct dictionary *, void*),
			  void *data)
{
  d->changed = changed;
  d->changed_data = data;
}

/* Discards dictionary D's caseproto.  (It will be regenerated
   lazily, on demand.) */
static void
invalidate_proto (struct dictionary *d)
{
  caseproto_unref (d->proto);
  d->proto = NULL;
}

/* Print a representation of dictionary D to stdout, for
   debugging purposes. */
void
dict_dump (const struct dictionary *d)
{
  int i;
  for (i = 0 ; i < d->var_cnt ; ++i )
    {
      const struct variable *v = d->var[i].var;
      printf ("Name: %s;\tdict_idx: %zu; case_idx: %zu\n",
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

/* Creates and returns a new dictionary with the specified ENCODING. */
struct dictionary *
dict_create (const char *encoding)
{
  struct dictionary *d = xzalloc (sizeof *d);

  d->encoding = xstrdup (encoding);
  hmap_init (&d->name_map);
  attrset_init (&d->attributes);

  return d;
}

/* Creates and returns a (deep) copy of an existing
   dictionary.

   The new dictionary's case indexes are copied from the old
   dictionary.  If the new dictionary won't be used to access
   cases produced with the old dictionary, then the new
   dictionary's case indexes should be compacted with
   dict_compact_values to save space.

   Callbacks are not cloned. */
struct dictionary *
dict_clone (const struct dictionary *s)
{
  struct dictionary *d;
  size_t i;

  d = dict_create (s->encoding);

  for (i = 0; i < s->var_cnt; i++)
    {
      struct variable *sv = s->var[i].var;
      struct variable *dv = dict_clone_var_assert (d, sv);
      size_t i;

      for (i = 0; i < var_get_short_name_cnt (sv); i++)
        var_set_short_name (dv, i, var_get_short_name (sv, i));

      var_get_vardict (dv)->case_index = var_get_vardict (sv)->case_index;
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

  dict_set_attributes (d, dict_get_attributes (s));

  for (i = 0; i < s->n_mrsets; i++)
    {
      const struct mrset *old = s->mrsets[i];
      struct mrset *new;
      size_t j;

      /* Clone old mrset, then replace vars from D by vars from S. */
      new = mrset_clone (old);
      for (j = 0; j < new->n_vars; j++)
        new->vars[j] = dict_lookup_var_assert (d, var_get_name (new->vars[j]));

      dict_add_mrset (d, new);
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
  while (d->var_cnt > 0 )
    {
      dict_delete_var (d, d->var[d->var_cnt - 1].var);
    }

  free (d->var);
  d->var = NULL;
  d->var_cnt = d->var_cap = 0;
  invalidate_proto (d);
  hmap_clear (&d->name_map);
  d->next_value_idx = 0;
  dict_set_split_vars (d, NULL, 0);
  dict_set_weight (d, NULL);
  dict_set_filter (d, NULL);
  d->case_limit = 0;
  free (d->label);
  d->label = NULL;
  string_array_clear (&d->documents);
  dict_clear_vectors (d);
  attrset_clear (&d->attributes);
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
      string_array_destroy (&d->documents);
      hmap_destroy (&d->name_map);
      attrset_destroy (&d->attributes);
      dict_clear_mrsets (d);
      free (d->encoding);
      free (d);
    }
}

/* Returns the number of variables in D. */
size_t
dict_get_var_cnt (const struct dictionary *d)
{
  return d->var_cnt;
}

/* Returns the variable in D with dictionary index IDX, which
   must be between 0 and the count returned by
   dict_get_var_cnt(), exclusive. */
struct variable *
dict_get_var (const struct dictionary *d, size_t idx)
{
  assert (idx < d->var_cnt);

  return d->var[idx].var;
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

  assert (exclude == (exclude & DC_ALL));

  count = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      enum dict_class class = var_get_dict_class (d->var[i].var);
      if (!(class & exclude))
        count++;
    }

  *vars = xnmalloc (count, sizeof **vars);
  *cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      enum dict_class class = var_get_dict_class (d->var[i].var);
      if (!(class & exclude))
        (*vars)[(*cnt)++] = d->var[i].var;
    }
  assert (*cnt == count);
}

static struct variable *
add_var_with_case_index (struct dictionary *d, struct variable *v,
                         int case_index)
{
  struct vardict_info *vardict;

  assert (case_index >= d->next_value_idx);

  /* Update dictionary. */
  if (d->var_cnt >= d->var_cap)
    {
      size_t i;

      d->var = x2nrealloc (d->var, &d->var_cap, sizeof *d->var);
      hmap_clear (&d->name_map);
      for (i = 0; i < d->var_cnt; i++)
        {
          var_set_vardict (d->var[i].var, &d->var[i]);
          hmap_insert_fast (&d->name_map, &d->var[i].name_node,
                            d->var[i].name_node.hash);
        }
    }

  vardict = &d->var[d->var_cnt++];
  vardict->dict = d;
  vardict->var = v;
  hmap_insert (&d->name_map, &vardict->name_node,
               utf8_hash_case_string (var_get_name (v), 0));
  vardict->case_index = case_index;
  var_set_vardict (v, vardict);

  if ( d->changed ) d->changed (d, d->changed_data);
  if ( d->callbacks &&  d->callbacks->var_added )
    d->callbacks->var_added (d, var_get_dict_index (v), d->cb_data);

  invalidate_proto (d);
  d->next_value_idx = case_index + 1;

  return v;
}

static struct variable *
add_var (struct dictionary *d, struct variable *v)
{
  return add_var_with_case_index (d, v, d->next_value_idx);
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

/* Creates and returns a new variable in D, as a copy of existing variable
   OLD_VAR, which need not be in D or in any dictionary.  Returns a null
   pointer if OLD_VAR's name would duplicate that of an existing variable in
   the dictionary. */
struct variable *
dict_clone_var (struct dictionary *d, const struct variable *old_var)
{
  return dict_clone_var_as (d, old_var, var_get_name (old_var));
}

/* Creates and returns a new variable in D, as a copy of existing variable
   OLD_VAR, which need not be in D or in any dictionary.  Assert-fails if
   OLD_VAR's name would duplicate that of an existing variable in the
   dictionary. */
struct variable *
dict_clone_var_assert (struct dictionary *d, const struct variable *old_var)
{
  return dict_clone_var_as_assert (d, old_var, var_get_name (old_var));
}

/* Creates and returns a new variable in D with name NAME, as a copy of
   existing variable OLD_VAR, which need not be in D or in any dictionary.
   Returns a null pointer if the given NAME would duplicate that of an existing
   variable in the dictionary. */
struct variable *
dict_clone_var_as (struct dictionary *d, const struct variable *old_var,
                   const char *name)
{
  return (dict_lookup_var (d, name) == NULL
          ? dict_clone_var_as_assert (d, old_var, name)
          : NULL);
}

/* Creates and returns a new variable in D with name NAME, as a copy of
   existing variable OLD_VAR, which need not be in D or in any dictionary.
   Assert-fails if the given NAME would duplicate that of an existing variable
   in the dictionary. */
struct variable *
dict_clone_var_as_assert (struct dictionary *d, const struct variable *old_var,
                          const char *name)
{
  struct variable *new_var = var_clone (old_var);
  assert (dict_lookup_var (d, name) == NULL);
  var_set_name (new_var, name);
  return add_var (d, new_var);
}

struct variable *
dict_clone_var_in_place_assert (struct dictionary *d,
                                const struct variable *old_var)
{
  assert (dict_lookup_var (d, var_get_name (old_var)) == NULL);
  return add_var_with_case_index (d, var_clone (old_var),
                                  var_get_case_index (old_var));
}

/* Returns the variable named NAME in D, or a null pointer if no
   variable has that name. */
struct variable *
dict_lookup_var (const struct dictionary *d, const char *name)
{
  struct vardict_info *vardict;

  HMAP_FOR_EACH_WITH_HASH (vardict, struct vardict_info, name_node,
                           utf8_hash_case_string (name, 0), &d->name_map)
    {
      struct variable *var = vardict->var;
      if (!utf8_strcasecmp (var_get_name (var), name))
        return var;
    }

  return NULL;
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
  return (var_has_vardict (v)
          && vardict_get_dictionary (var_get_vardict (v)) == d);
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

static void
unindex_var (struct dictionary *d, struct vardict_info *vardict)
{
  hmap_delete (&d->name_map, &vardict->name_node);
}

/* This function assumes that vardict->name_node.hash is valid, that is, that
   its name has not changed since it was hashed (rename_var() updates this
   hash along with the name itself). */
static void
reindex_var (struct dictionary *d, struct vardict_info *vardict)
{
  struct variable *var = vardict->var;
  struct variable *old = var_clone (var);

  var_set_vardict (var, vardict);
  hmap_insert_fast (&d->name_map, &vardict->name_node,
                    vardict->name_node.hash);

  if ( d->changed ) d->changed (d, d->changed_data);
  if ( d->callbacks &&  d->callbacks->var_changed )
    d->callbacks->var_changed (d, var_get_dict_index (var), VAR_TRAIT_POSITION, old, d->cb_data);
  var_destroy (old);
}

/* Sets the case_index in V's vardict to CASE_INDEX. */
static void
set_var_case_index (struct variable *v, int case_index)
{
  var_get_vardict (v)->case_index = case_index;
}

/* Removes the dictionary variables with indexes from FROM to TO (exclusive)
   from name_map. */
static void
unindex_vars (struct dictionary *d, size_t from, size_t to)
{
  size_t i;

  for (i = from; i < to; i++)
    unindex_var (d, &d->var[i]);
}

/* Re-sets the dict_index in the dictionary variables with
   indexes from FROM to TO (exclusive). */
static void
reindex_vars (struct dictionary *d, size_t from, size_t to)
{
  size_t i;

  for (i = from; i < to; i++)
    reindex_var (d, &d->var[i]);
}

/* Deletes variable V from dictionary D and frees V.

   This is a very bad idea if there might be any pointers to V
   from outside D.  In general, no variable in the active dataset's
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

  assert (dict_contains_var (d, v));

  dict_unset_split_var (d, v);
  dict_unset_mrset_var (d, v);

  if (d->weight == v)
    dict_set_weight (d, NULL);

  if (d->filter == v)
    dict_set_filter (d, NULL);

  dict_clear_vectors (d);

  /* Remove V from var array. */
  unindex_vars (d, dict_index, d->var_cnt);
  remove_element (d->var, d->var_cnt, sizeof *d->var, dict_index);
  d->var_cnt--;

  /* Update dict_index for each affected variable. */
  reindex_vars (d, dict_index, d->var_cnt);

  /* Free memory. */
  var_clear_vardict (v);

  if ( d->changed ) d->changed (d, d->changed_data);

  invalidate_proto (d);
  if (d->callbacks &&  d->callbacks->var_deleted )
    d->callbacks->var_deleted (d, v, dict_index, case_index, d->cb_data);

  var_destroy (v);
}

/* Deletes the COUNT variables listed in VARS from D.  This is
   unsafe; see the comment on dict_delete_var() for details. */
void
dict_delete_vars (struct dictionary *d,
                  struct variable *const *vars, size_t count)
{
  /* FIXME: this can be done in O(count) time, but this algorithm
     is O(count**2). */
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
    dict_delete_var (d, d->var[idx].var);
}

/* Deletes scratch variables from dictionary D. */
void
dict_delete_scratch_vars (struct dictionary *d)
{
  int i;

  /* FIXME: this can be done in O(count) time, but this algorithm
     is O(count**2). */
  for (i = 0; i < d->var_cnt; )
    if (var_get_dict_class (d->var[i].var) == DC_SCRATCH)
      dict_delete_var (d, d->var[i].var);
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

  unindex_vars (d, MIN (old_index, new_index), MAX (old_index, new_index) + 1);
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
  struct vardict_info *new_var;
  size_t i;

  assert (count == 0 || order != NULL);
  assert (count <= d->var_cnt);

  new_var = xnmalloc (d->var_cap, sizeof *new_var);

  /* Add variables in ORDER to new_var. */
  for (i = 0; i < count; i++)
    {
      struct vardict_info *old_var;

      assert (dict_contains_var (d, order[i]));

      old_var = var_get_vardict (order[i]);
      new_var[i] = *old_var;
      old_var->dict = NULL;
    }

  /* Add remaining variables to new_var. */
  for (i = 0; i < d->var_cnt; i++)
    if (d->var[i].dict != NULL)
      new_var[count++] = d->var[i];
  assert (count == d->var_cnt);

  /* Replace old vardicts by new ones. */
  free (d->var);
  d->var = new_var;

  hmap_clear (&d->name_map);
  reindex_vars (d, 0, d->var_cnt);
}

/* Changes the name of variable V that is currently in a dictionary to
   NEW_NAME. */
static void
rename_var (struct variable *v, const char *new_name)
{
  struct vardict_info *vardict = var_get_vardict (v);
  var_clear_vardict (v);
  var_set_name (v, new_name);
  vardict->name_node.hash = utf8_hash_case_string (new_name, 0);
  var_set_vardict (v, vardict);
}

/* Changes the name of V in D to name NEW_NAME.  Assert-fails if
   a variable named NEW_NAME is already in D, except that
   NEW_NAME may be the same as V's existing name. */
void
dict_rename_var (struct dictionary *d, struct variable *v,
                 const char *new_name)
{
  struct variable *old = var_clone (v);
  assert (!utf8_strcasecmp (var_get_name (v), new_name)
          || dict_lookup_var (d, new_name) == NULL);

  unindex_var (d, var_get_vardict (v));
  rename_var (v, new_name);
  reindex_var (d, var_get_vardict (v));

  if (settings_get_algorithm () == ENHANCED)
    var_clear_short_names (v);

  if ( d->changed ) d->changed (d, d->changed_data);
  if ( d->callbacks &&  d->callbacks->var_changed )
    d->callbacks->var_changed (d, var_get_dict_index (v), VAR_TRAIT_NAME, old, d->cb_data);

  var_destroy (old);
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
      unindex_var (d, var_get_vardict (vars[i]));
      rename_var (vars[i], new_names[i]);
    }

  /* Add the renamed variables back into the name hash,
     checking for conflicts. */
  for (i = 0; i < count; i++)
    {
      if (dict_lookup_var (d, var_get_name (vars[i])) != NULL)
        {
          /* There is a name conflict.
             Back out all the name changes that have already
             taken place, and indicate failure. */
          size_t fail_idx = i;
          if (err_name != NULL)
            *err_name = new_names[i];

          for (i = 0; i < fail_idx; i++)
            unindex_var (d, var_get_vardict (vars[i]));

          for (i = 0; i < count; i++)
            {
              rename_var (vars[i], old_names[i]);
              reindex_var (d, var_get_vardict (vars[i]));
            }

          pool_destroy (pool);
          return false;
        }
      reindex_var (d, var_get_vardict (vars[i]));
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

static char *
make_hinted_name (const struct dictionary *dict, const char *hint)
{
  size_t hint_len = strlen (hint);
  bool dropped = false;
  char *root, *rp;
  size_t ofs;
  int mblen;

  /* The allocation size here is OK: characters that are copied directly fit
     OK, and characters that are not copied directly are replaced by a single
     '_' byte.  If u8_mbtouc() replaces bad input by 0xfffd, then that will get
     replaced by '_' too.  */
  root = rp = xmalloc (hint_len + 1);
  for (ofs = 0; ofs < hint_len; ofs += mblen)
    {
      ucs4_t uc;

      mblen = u8_mbtouc (&uc, CHAR_CAST (const uint8_t *, hint + ofs),
                         hint_len - ofs);
      if (rp == root
          ? lex_uc_is_id1 (uc) && uc != '$'
          : lex_uc_is_idn (uc))
        {
          if (dropped)
            {
              *rp++ = '_';
              dropped = false;
            }
          rp += u8_uctomb (CHAR_CAST (uint8_t *, rp), uc, 6);
        }
      else if (rp != root)
        dropped = true;
    }
  *rp = '\0';

  if (root[0] != '\0')
    {
      unsigned long int i;

      if (var_name_is_insertable (dict, root))
        return root;

      for (i = 0; i < ULONG_MAX; i++)
        {
          char suffix[INT_BUFSIZE_BOUND (i) + 1];
          char *name;

          suffix[0] = '_';
          if (!str_format_26adic (i + 1, true, &suffix[1], sizeof suffix - 1))
            NOT_REACHED ();

          name = utf8_encoding_concat (root, suffix, dict->encoding, 64);
          if (var_name_is_insertable (dict, name))
            {
              free (root);
              return name;
            }
          free (name);
        }
    }

  free (root);

  return NULL;
}

static char *
make_numeric_name (const struct dictionary *dict, unsigned long int *num_start)
{
  unsigned long int number;

  for (number = num_start != NULL ? MAX (*num_start, 1) : 1;
       number < ULONG_MAX;
       number++)
    {
      char name[3 + INT_STRLEN_BOUND (number) + 1];

      sprintf (name, "VAR%03lu", number);
      if (dict_lookup_var (dict, name) == NULL)
        {
          if (num_start != NULL)
            *num_start = number + 1;
          return xstrdup (name);
        }
    }

  NOT_REACHED ();
}


/* Devises and returns a variable name unique within DICT.  The variable name
   is owned by the caller, which must free it with free() when it is no longer
   needed.

   HINT, if it is non-null, is used as a suggestion that will be
   modified for suitability as a variable name and for
   uniqueness.

   If HINT is null or entirely unsuitable, a name in the form
   "VAR%03d" will be generated, where the smallest unused integer
   value is used.  If NUM_START is non-null, then its value is
   used as the minimum numeric value to check, and it is updated
   to the next value to be checked.
*/
char *
dict_make_unique_var_name (const struct dictionary *dict, const char *hint,
                           unsigned long int *num_start)
{
  if (hint != NULL)
    {
      char *hinted_name = make_hinted_name (dict, hint);
      if (hinted_name != NULL)
        return hinted_name;
    }
  return make_numeric_name (dict, num_start);
}

/* Returns the weighting variable in dictionary D, or a null
   pointer if the dictionary is unweighted. */
struct variable *
dict_get_weight (const struct dictionary *d)
{
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
  assert (d->filter == NULL || dict_contains_var (d, d->filter));

  return d->filter;
}

/* Sets V as the filter variable for dictionary D.  Passing a
   null pointer for V turn off filtering. */
void
dict_set_filter (struct dictionary *d, struct variable *v)
{
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
  return d->case_limit;
}

/* Sets CASE_LIMIT as the case limit for dictionary D.  Use
   0 for CASE_LIMIT to indicate no limit. */
void
dict_set_case_limit (struct dictionary *d, casenumber case_limit)
{
  d->case_limit = case_limit;
}

/* Returns the prototype used for cases created by dictionary D. */
const struct caseproto *
dict_get_proto (const struct dictionary *d_)
{
  struct dictionary *d = CONST_CAST (struct dictionary *, d_);
  if (d->proto == NULL)
    {
      size_t i;

      d->proto = caseproto_create ();
      d->proto = caseproto_reserve (d->proto, d->var_cnt);
      for (i = 0; i < d->var_cnt; i++)
        d->proto = caseproto_set_width (d->proto,
                                        var_get_case_index (d->var[i].var),
                                        var_get_width (d->var[i].var));
    }
  return d->proto;
}

/* Returns the case index of the next value to be added to D.
   This value is the number of `union value's that need to be
   allocated to store a case for dictionary D. */
int
dict_get_next_value_idx (const struct dictionary *d)
{
  return d->next_value_idx;
}

/* Returns the number of bytes needed to store a case for
   dictionary D. */
size_t
dict_get_case_size (const struct dictionary *d)
{
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
      struct variable *v = d->var[i].var;
      set_var_case_index (v, d->next_value_idx++);
    }
  invalidate_proto (d);
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
      enum dict_class class = var_get_dict_class (d->var[i].var);
      if (!(exclude_classes & (1u << class)))
        cnt++;
    }
  return cnt;
}

/* Returns the case prototype that would result after deleting
   all variables from D that are not in one of the
   EXCLUDE_CLASSES and compacting the dictionary with
   dict_compact().

   The caller must unref the returned caseproto when it is no
   longer needed. */
struct caseproto *
dict_get_compacted_proto (const struct dictionary *d,
                          unsigned int exclude_classes)
{
  struct caseproto *proto;
  size_t i;

  assert ((exclude_classes & ~((1u << DC_ORDINARY)
                               | (1u << DC_SYSTEM)
                               | (1u << DC_SCRATCH))) == 0);

  proto = caseproto_create ();
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i].var;
      if (!(exclude_classes & (1u << var_get_dict_class (v))))
        proto = caseproto_add_width (proto, var_get_width (v));
    }
  return proto;
}

/* Returns the SPLIT FILE vars (see cmd_split_file()).  Call
   dict_get_split_cnt() to determine how many SPLIT FILE vars
   there are.  Returns a null pointer if and only if there are no
   SPLIT FILE vars. */
const struct variable *const *
dict_get_split_vars (const struct dictionary *d)
{
  return d->split;
}

/* Returns the number of SPLIT FILE vars. */
size_t
dict_get_split_cnt (const struct dictionary *d)
{
  return d->split_cnt;
}

/* Removes variable V, which must be in D, from D's set of split
   variables. */
static void
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
  return d->label;
}

/* Sets D's file label to LABEL, truncating it to at most 60 bytes in D's
   encoding.

   Removes D's label if LABEL is null or the empty string. */
void
dict_set_label (struct dictionary *d, const char *label)
{
  free (d->label);
  if (label == NULL || label[0] == '\0')
    d->label = NULL;
  else
    d->label = utf8_encoding_trunc (label, d->encoding, 60);
}

/* Returns the documents for D, as an UTF-8 encoded string_array.  The
   return value is always nonnull; if there are no documents then the
   string_arary is empty.*/
const struct string_array *
dict_get_documents (const struct dictionary *d)
{
  return &d->documents;
}

/* Replaces the documents for D by NEW_DOCS, a UTF-8 encoded string_array. */
void
dict_set_documents (struct dictionary *d, const struct string_array *new_docs)
{
  size_t i;

  dict_clear_documents (d);

  for (i = 0; i < new_docs->n; i++)
    dict_add_document_line (d, new_docs->strings[i], false);
}

/* Replaces the documents for D by UTF-8 encoded string NEW_DOCS, dividing it
   into individual lines at new-line characters.  Each line is truncated to at
   most DOC_LINE_LENGTH bytes in D's encoding. */
void
dict_set_documents_string (struct dictionary *d, const char *new_docs)
{
  const char *s;

  dict_clear_documents (d);
  for (s = new_docs; *s != '\0'; )
    {
      size_t len = strcspn (s, "\n");
      char *line = xmemdup0 (s, len);
      dict_add_document_line (d, line, false);
      free (line);

      s += len;
      if (*s == '\n')
        s++;
    }
}

/* Drops the documents from dictionary D. */
void
dict_clear_documents (struct dictionary *d)
{
  string_array_clear (&d->documents);
}

/* Appends the UTF-8 encoded LINE to the documents in D.  LINE will be
   truncated so that it is no more than 80 bytes in the dictionary's
   encoding.  If this causes some text to be lost, and ISSUE_WARNING is true,
   then a warning will be issued. */
bool
dict_add_document_line (struct dictionary *d, const char *line,
                        bool issue_warning)
{
  size_t trunc_len;
  bool truncated;

  trunc_len = utf8_encoding_trunc_len (line, d->encoding, DOC_LINE_LENGTH);
  truncated = line[trunc_len] != '\0';
  if (truncated && issue_warning)
    {
      /* Note to translators: "bytes" is correct, not characters */
      msg (SW, _("Truncating document line to %d bytes."), DOC_LINE_LENGTH);
    }

  string_array_append_nocopy (&d->documents, xmemdup0 (line, trunc_len));

  return !truncated;
}

/* Returns the number of document lines in dictionary D. */
size_t
dict_get_document_line_cnt (const struct dictionary *d)
{
  return d->documents.n;
}

/* Returns document line number IDX in dictionary D.  The caller must not
   modify or free the returned string. */
const char *
dict_get_document_line (const struct dictionary *d, size_t idx)
{
  assert (idx < d->documents.n);
  return d->documents.strings[idx];
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
  assert (idx < d->vector_cnt);

  return d->vector[idx];
}

/* Returns the number of vectors in D. */
size_t
dict_get_vector_cnt (const struct dictionary *d)
{
  return d->vector_cnt;
}

/* Looks up and returns the vector within D with the given
   NAME. */
const struct vector *
dict_lookup_vector (const struct dictionary *d, const char *name)
{
  size_t i;
  for (i = 0; i < d->vector_cnt; i++)
    if (!utf8_strcasecmp (vector_get_name (d->vector[i]), name))
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

/* Multiple response sets. */

/* Returns the multiple response set in DICT with index IDX, which must be
   between 0 and the count returned by dict_get_n_mrsets(), exclusive. */
const struct mrset *
dict_get_mrset (const struct dictionary *dict, size_t idx)
{
  assert (idx < dict->n_mrsets);
  return dict->mrsets[idx];
}

/* Returns the number of multiple response sets in DICT. */
size_t
dict_get_n_mrsets (const struct dictionary *dict)
{
  return dict->n_mrsets;
}

/* Looks for a multiple response set named NAME in DICT.  If it finds one,
   returns its index; otherwise, returns SIZE_MAX. */
static size_t
dict_lookup_mrset_idx (const struct dictionary *dict, const char *name)
{
  size_t i;

  for (i = 0; i < dict->n_mrsets; i++)
    if (!utf8_strcasecmp (name, dict->mrsets[i]->name))
      return i;

  return SIZE_MAX;
}

/* Looks for a multiple response set named NAME in DICT.  If it finds one,
   returns it; otherwise, returns NULL. */
const struct mrset *
dict_lookup_mrset (const struct dictionary *dict, const char *name)
{
  size_t idx = dict_lookup_mrset_idx (dict, name);
  return idx != SIZE_MAX ? dict->mrsets[idx] : NULL;
}

/* Adds MRSET to DICT, replacing any existing set with the same name.  Returns
   true if a set was replaced, false if none existed with the specified name.

   Ownership of MRSET is transferred to DICT. */
bool
dict_add_mrset (struct dictionary *dict, struct mrset *mrset)
{
  size_t idx;

  assert (mrset_ok (mrset, dict));

  idx = dict_lookup_mrset_idx (dict, mrset->name);
  if (idx == SIZE_MAX)
    {
      dict->mrsets = xrealloc (dict->mrsets,
                               (dict->n_mrsets + 1) * sizeof *dict->mrsets);
      dict->mrsets[dict->n_mrsets++] = mrset;
      return true;
    }
  else
    {
      mrset_destroy (dict->mrsets[idx]);
      dict->mrsets[idx] = mrset;
      return false;
    }
}

/* Looks for a multiple response set in DICT named NAME.  If found, removes it
   from DICT and returns true.  If none is found, returns false without
   modifying DICT.

   Deleting one multiple response set causes the indexes of other sets within
   DICT to change. */
bool
dict_delete_mrset (struct dictionary *dict, const char *name)
{
  size_t idx = dict_lookup_mrset_idx (dict, name);
  if (idx != SIZE_MAX)
    {
      mrset_destroy (dict->mrsets[idx]);
      dict->mrsets[idx] = dict->mrsets[--dict->n_mrsets];
      return true;
    }
  else
    return false;
}

/* Deletes all multiple response sets from DICT. */
void
dict_clear_mrsets (struct dictionary *dict)
{
  size_t i;

  for (i = 0; i < dict->n_mrsets; i++)
    mrset_destroy (dict->mrsets[i]);
  free (dict->mrsets);
  dict->mrsets = NULL;
  dict->n_mrsets = 0;
}

/* Removes VAR, which must be in DICT, from DICT's multiple response sets. */
static void
dict_unset_mrset_var (struct dictionary *dict, struct variable *var)
{
  size_t i;

  assert (dict_contains_var (dict, var));

  for (i = 0; i < dict->n_mrsets; )
    {
      struct mrset *mrset = dict->mrsets[i];
      size_t j;

      for (j = 0; j < mrset->n_vars; )
        if (mrset->vars[j] == var)
          remove_element (mrset->vars, mrset->n_vars--,
                          sizeof *mrset->vars, j);
        else
          j++;

      if (mrset->n_vars < 2)
        {
          mrset_destroy (mrset);
          dict->mrsets[i] = dict->mrsets[--dict->n_mrsets];
        }
      else
        i++;
    }
}

/* Returns D's attribute set.  The caller may examine or modify
   the attribute set, but must not destroy it.  Destroying D or
   calling dict_set_attributes for D will also destroy D's
   attribute set. */
struct attrset *
dict_get_attributes (const struct dictionary *d) 
{
  return CONST_CAST (struct attrset *, &d->attributes);
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

/* Called from variable.c to notify the dictionary that some property (indicated
   by WHAT) of the variable has changed.  OLDVAR is a copy of V as it existed
   prior to the change.  OLDVAR is destroyed by this function.
*/
void
dict_var_changed (const struct variable *v, unsigned int what, struct variable *oldvar)
{
  if ( var_has_vardict (v))
    {
      const struct vardict_info *vardict = var_get_vardict (v);
      struct dictionary *d = vardict->dict;

      if ( NULL == d)
	return;

      if (d->changed ) d->changed (d, d->changed_data);
      if ( d->callbacks && d->callbacks->var_changed )
	d->callbacks->var_changed (d, var_get_dict_index (v), what, oldvar, d->cb_data);
    }
  var_destroy (oldvar);
}



/* Dictionary used to contain "internal variables". */
static struct dictionary *internal_dict;

/* Create a variable of the specified WIDTH to be used for internal
   calculations only.  The variable is assigned case index CASE_IDX. */
struct variable *
dict_create_internal_var (int case_idx, int width)
{
  if (internal_dict == NULL)
    internal_dict = dict_create ("UTF-8");

  for (;;)
    {
      static int counter = INT_MAX / 2;
      struct variable *var;
      char name[64];

      if (++counter == INT_MAX)
        counter = INT_MAX / 2;

      sprintf (name, "$internal%d", counter);
      var = dict_create_var (internal_dict, name, width);
      if (var != NULL)
        {
          set_var_case_index (var, case_idx);
          return var;
        }
    }
}

/* Destroys VAR, which must have been created with
   dict_create_internal_var(). */
void
dict_destroy_internal_var (struct variable *var)
{
  if (var != NULL)
    {
      dict_delete_var (internal_dict, var);

      /* Destroy internal_dict if it has no variables left, just so that
         valgrind --leak-check --show-reachable won't show internal_dict. */
      if (dict_get_var_cnt (internal_dict) == 0)
        {
          dict_destroy (internal_dict);
          internal_dict = NULL;
        }
    }
}

int
vardict_get_dict_index (const struct vardict_info *vardict)
{
  return vardict - vardict->dict->var;
}
