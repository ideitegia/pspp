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
#include "algorithm.h"
#include "alloc.h"
#include "hash.h"
#include "misc.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"

/* A dictionary. */
struct dictionary
  {
    struct variable **var;	/* Variables. */
    size_t var_cnt, var_cap;    /* Number of variables, capacity. */
    struct hsh_table *name_tab;	/* Variable index by name. */
    int value_cnt;              /* Number of `union value's per case. */
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

struct dictionary *
dict_create (void) 
{
  struct dictionary *d = xmalloc (sizeof *d);
  
  d->var = NULL;
  d->var_cnt = d->var_cap = 0;
  d->name_tab = hsh_create (8, compare_variables, hash_variable, NULL, NULL);
  d->value_cnt = 0;
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

struct dictionary *
dict_clone (const struct dictionary *s) 
{
  struct dictionary *d;
  size_t i;
  
  assert (s != NULL);
  
  d = dict_create ();
  for (i = 0; i < s->var_cnt; i++)
    dict_clone_var (d, s->var[i], s->var[i]->name);
  d->value_cnt = s->value_cnt;

  d->split_cnt = s->split_cnt;
  if (d->split_cnt > 0) 
    {
      d->split = xmalloc (d->split_cnt * sizeof *d->split);
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

  for (i = 0; i < s->vector_cnt; i++) 
    dict_create_vector (d, s->vector[i]->name,
                        s->vector[i]->var, s->vector[i]->cnt);

  return d;
}

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
      val_labs_destroy (v->val_labs);
      free (v->label);
      free (v); 
    }
  free (d->var);
  d->var = NULL;
  d->var_cnt = d->var_cap = 0;
  hsh_clear (d->name_tab);
  d->value_cnt = 0;
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

size_t
dict_get_var_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->var_cnt;
}

struct variable *
dict_get_var (const struct dictionary *d, size_t idx) 
{
  assert (d != NULL);
  assert (idx < d->var_cnt);

  return d->var[idx];
}

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

  *vars = xmalloc (count * sizeof **vars);
  *cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    if (!(exclude_classes & (1u << dict_class_from_id (d->var[i]->name))))
      (*vars)[(*cnt)++] = d->var[i];
  assert (*cnt == count);
}

struct variable *
dict_create_var (struct dictionary *d, const char *name, int width) 
{
  struct variable *v;

  assert (d != NULL);
  assert (name != NULL);
  assert (strlen (name) >= 1 && strlen (name) <= 8);
  assert (width >= 0 && width < 256);

  /* Make sure there's not already a variable by that name. */
  if (dict_lookup_var (d, name) != NULL)
    return NULL;

  /* Allocate and initialize variable. */
  v = xmalloc (sizeof *v);
  strncpy (v->name, name, sizeof v->name);
  v->name[8] = '\0';
  v->index = d->var_cnt;
  v->type = width == 0 ? NUMERIC : ALPHA;
  v->width = width;
  v->fv = d->value_cnt;
  v->nv = width == 0 ? 1 : DIV_RND_UP (width, 8);
  v->init = 1;
  v->reinit = name[0] != '#';
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
  v->val_labs = val_labs_create (v->width);
  v->label = NULL;

  /* Update dictionary. */
  if (d->var_cnt >= d->var_cap) 
    {
      d->var_cap = 8 + 2 * d->var_cap; 
      d->var = xrealloc (d->var, d->var_cap * sizeof *d->var);
    }
  d->var[v->index] = v;
  d->var_cnt++;
  hsh_force_insert (d->name_tab, v);
  d->value_cnt += v->nv;

  return v;
}

struct variable *
dict_create_var_assert (struct dictionary *d, const char *name, int width) 
{
  struct variable *v = dict_create_var (d, name, width);
  assert (v != NULL);
  return v;
}

struct variable *
dict_clone_var (struct dictionary *d, const struct variable *ov,
                const char *name)
{
  struct variable *nv;

  assert (d != NULL);
  assert (ov != NULL);
  assert (name != NULL);
  assert (strlen (name) >= 1 && strlen (name) <= 8);

  nv = dict_create_var (d, name, ov->width);
  if (nv == NULL)
    return NULL;

  nv->init = 1;
  nv->reinit = ov->reinit;
  nv->miss_type = ov->miss_type;
  memcpy (nv->missing, ov->missing, sizeof nv->missing);
  nv->print = ov->print;
  nv->write = ov->write;
  val_labs_destroy (nv->val_labs);
  nv->val_labs = val_labs_copy (ov->val_labs);
  if (ov->label != NULL)
    nv->label = xstrdup (ov->label);

  return nv;
}

void 
dict_rename_var (struct dictionary *d, struct variable *v,
                 const char *new_name) 
{
  assert (d != NULL);
  assert (v != NULL);
  assert (new_name != NULL);
  assert (strlen (new_name) >= 1 && strlen (new_name) <= 8);

  if (!strcmp (v->name, new_name))
    return;

  assert (dict_lookup_var (d, new_name) == NULL);

  hsh_force_delete (d->name_tab, v);
  strncpy (v->name, new_name, sizeof v->name);
  v->name[8] = '\0';
  hsh_force_insert (d->name_tab, v);
}

struct variable *
dict_lookup_var (const struct dictionary *d, const char *name)
{
  struct variable v;
  
  assert (d != NULL);
  assert (name != NULL);
  assert (strlen (name) >= 1 && strlen (name) <= 8);

  strncpy (v.name, name, sizeof v.name);
  v.name[8] = '\0';

  return hsh_find (d->name_tab, &v);
}

struct variable *
dict_lookup_var_assert (const struct dictionary *d, const char *name)
{
  struct variable *v = dict_lookup_var (d, name);
  assert (v != NULL);
  return v;
}

int
dict_contains_var (const struct dictionary *d, const struct variable *v)
{
  assert (d != NULL);
  assert (v != NULL);

  return dict_lookup_var (d, v->name) == v;
}

static int
compare_variable_dblptrs (const void *a_, const void *b_, void *aux UNUSED) 
{
  struct variable *const *a = a_;
  struct variable *const *b = b_;

  if (a > b)
    return 1;
  else if (a < b)
    return -1;
  else
    return 0;
}

void
dict_delete_var (struct dictionary *d, struct variable *v) 
{
  size_t i;

  /* FIXME?  Does not sync d->value_cnt. */
  assert (d != NULL);
  assert (v != NULL);
  assert (dict_contains_var (d, v));
  assert (d->var[v->index] == v);

  /* Remove v from splits, weight, filter variables. */
  d->split_cnt = remove_equal (d->split, d->split_cnt, sizeof *d->split,
                               &v,
                               compare_variable_dblptrs, NULL);
  if (d->weight == v)
    d->weight = NULL;
  if (d->filter == v)
    d->filter = NULL;
  dict_clear_vectors (d);

  /* Remove v from var array. */
  d->var_cnt--;
  memmove (d->var + v->index, d->var + v->index + 1,
           (d->var_cnt - v->index) * sizeof *d->var);

  /* Update index. */
  for (i = v->index; i < d->var_cnt; i++)
    d->var[i]->index = i;

  /* Update name hash. */
  hsh_force_delete (d->name_tab, v);

  /* Free memory. */
  val_labs_destroy (v->val_labs);
  free (v->label);
  free (v);

}

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

void 
dict_reorder_vars (struct dictionary *d,
                   struct variable *const *order, size_t count) 
{
  struct variable **new_var;
  size_t i;
  
  assert (d != NULL);
  assert (count == 0 || order != NULL);
  assert (count <= d->var_cnt);

  new_var = xmalloc (d->var_cnt * sizeof *new_var);
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

int
dict_rename_vars (struct dictionary *d,
                  struct variable **vars, char **new_names,
                  size_t count, char **err_name) 
{
  char **old_names;
  size_t i;
  int success = 1;

  assert (d != NULL);
  assert (count == 0 || vars != NULL);
  assert (count == 0 || new_names != NULL);

  old_names = xmalloc (count * sizeof *old_names);
  for (i = 0; i < count; i++) 
    {
      assert (d->var[vars[i]->index] == vars[i]);
      hsh_force_delete (d->name_tab, vars[i]);
      old_names[i] = xstrdup (vars[i]->name);
    }
  
  for (i = 0; i < count; i++)
    {
      assert (new_names[i] != NULL);
      assert (strlen (new_names[i]) < 9);
      strcpy (vars[i]->name, new_names[i]);
      if (hsh_insert (d->name_tab, vars[i]) != NULL) 
        {
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

          success = 0;
          break;
        }
    }

  for (i = 0; i < count; i++)
    free (old_names[i]);
  free (old_names);

  return success;
}

struct variable *
dict_get_weight (const struct dictionary *d) 
{
  assert (d != NULL);
  assert (d->weight == NULL || dict_contains_var (d, d->weight));
  
  return d->weight;
}

double
dict_get_case_weight (const struct dictionary *d, const struct ccase *c)
{
  assert (d != NULL);
  assert (c != NULL);

  if (d->weight == NULL)
    return 1.0;
  else 
    {
      double w = c->data[d->weight->fv].f;
      if (w < 0.0)
        w = 0.0;
      return w;
    }
}

void
dict_set_weight (struct dictionary *d, struct variable *v) 
{
  assert (d != NULL);
  assert (v == NULL || dict_contains_var (d, v));
  assert (v == NULL || v->type == NUMERIC);

  d->weight = v;
}

struct variable *
dict_get_filter (const struct dictionary *d) 
{
  assert (d != NULL);
  assert (d->filter == NULL || dict_contains_var (d, d->filter));
  
  return d->filter;
}

void
dict_set_filter (struct dictionary *d, struct variable *v)
{
  assert (d != NULL);
  assert (v == NULL || dict_contains_var (d, v));

  d->filter = v;
}

int
dict_get_case_limit (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->case_limit;
}

void
dict_set_case_limit (struct dictionary *d, int case_limit) 
{
  assert (d != NULL);
  assert (case_limit >= 0);

  d->case_limit = case_limit;
}

int
dict_get_value_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->value_cnt;
}

void
dict_compact_values (struct dictionary *d) 
{
  size_t i;

  d->value_cnt = 0;
  for (i = 0; i < d->var_cnt; i++)
    {
      struct variable *v = d->var[i];

      v->fv = d->value_cnt;
      d->value_cnt += v->nv;
    }
}

struct variable *const *
dict_get_split_vars (const struct dictionary *d) 
{
  assert (d != NULL);
  
  return d->split;
}

size_t
dict_get_split_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->split_cnt;
}

void
dict_set_split_vars (struct dictionary *d,
                     struct variable *const *split, size_t cnt)
{
  assert (d != NULL);
  assert (cnt == 0 || split != NULL);

  d->split_cnt = cnt;
  d->split = xrealloc (d->split, cnt * sizeof *d->split);
  memcpy (d->split, split, cnt * sizeof *d->split);
}

const char *
dict_get_label (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->label;
}

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

const char *
dict_get_documents (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->documents;
}

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

int
dict_create_vector (struct dictionary *d,
                    const char *name,
                    struct variable **var, size_t cnt) 
{
  struct vector *vector;

  assert (d != NULL);
  assert (name != NULL);
  assert (strlen (name) > 0 && strlen (name) < 9);
  assert (var != NULL);
  assert (cnt > 0);
  
  if (dict_lookup_vector (d, name) != NULL)
    return 0;

  d->vector = xrealloc (d->vector, (d->vector_cnt + 1) * sizeof *d->vector);
  vector = d->vector[d->vector_cnt] = xmalloc (sizeof *vector);
  vector->idx = d->vector_cnt++;
  strncpy (vector->name, name, 8);
  vector->name[8] = '\0';
  vector->var = xmalloc (cnt * sizeof *var);
  memcpy (vector->var, var, cnt * sizeof *var);
  vector->cnt = cnt;
  
  return 1;
}

const struct vector *
dict_get_vector (const struct dictionary *d, size_t idx) 
{
  assert (d != NULL);
  assert (idx < d->vector_cnt);

  return d->vector[idx];
}

size_t
dict_get_vector_cnt (const struct dictionary *d) 
{
  assert (d != NULL);

  return d->vector_cnt;
}

const struct vector *
dict_lookup_vector (const struct dictionary *d, const char *name) 
{
  size_t i;

  assert (d != NULL);
  assert (name != NULL);

  for (i = 0; i < d->vector_cnt; i++)
    if (!strcmp (d->vector[i]->name, name))
      return d->vector[i];
  return NULL;
}

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
