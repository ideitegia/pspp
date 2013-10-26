/* PSPP - a program for statistical analysis.
   Copyright (C) 2010, 2011, 2012, 2013 Free Software Foundation, Inc.

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

#include "data/session.h"

#include <assert.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "libpspp/assertion.h"
#include "libpspp/cast.h"
#include "libpspp/hash-functions.h"
#include "libpspp/i18n.h"
#include "libpspp/str.h"
#include "libpspp/hmapx.h"

#include "gl/xalloc.h"

struct session
  {
    struct session *parent;
    struct hmapx datasets;
    struct dataset *active;
    char *syntax_encoding;      /* Default encoding for syntax files. */
    unsigned int n_dataset_names; /* For session_generate_dataset_name(). */
  };

static struct hmapx_node *session_lookup_dataset__ (const struct session *,
                                                    const char *name);

struct session *
session_create (struct session *parent)
{
  struct session *s;

  s = xmalloc (sizeof *s);
  s->parent = parent;
  hmapx_init (&s->datasets);
  s->active = NULL;
  s->syntax_encoding = xstrdup (s->parent != NULL
                                ? s->parent->syntax_encoding : "Auto");
  s->n_dataset_names = 0;
  return s;
}

void
session_destroy (struct session *s)
{
  if (s != NULL)
    {
      struct hmapx_node *node, *next;
      struct dataset *ds;

      s->active = NULL;
      HMAPX_FOR_EACH_SAFE (ds, node, next, &s->datasets)
        dataset_destroy (ds);
      hmapx_destroy (&s->datasets);
      free (s->syntax_encoding);
      free (s);
    }
}

struct dataset *
session_active_dataset (struct session *s)
{
  return s->active;
}

void
session_set_active_dataset (struct session *s, struct dataset *ds)
{
  assert (ds == NULL || dataset_session (ds) == s);
  s->active = ds;
}

void
session_add_dataset (struct session *s, struct dataset *ds)
{
  struct dataset *old;

  old = session_lookup_dataset (s, dataset_name (ds));
  if (old == s->active)
    s->active = ds;
  if (old != NULL)
    {
      session_remove_dataset (s, old);
      dataset_destroy (old);
    }

  hmapx_insert (&s->datasets, ds,
                utf8_hash_case_string (dataset_name (ds), 0));
  if (s->active == NULL)
    s->active = ds;

  dataset_set_session__ (ds, s);
}

void
session_remove_dataset (struct session *s, struct dataset *ds)
{
  assert (ds != s->active);
  hmapx_delete (&s->datasets, session_lookup_dataset__ (s, dataset_name (ds)));
  dataset_set_session__ (ds, NULL);
}

struct dataset *
session_lookup_dataset (const struct session *s, const char *name)
{
  struct hmapx_node *node = session_lookup_dataset__ (s, name);
  return (node != NULL ? node->data
          : s->parent != NULL ? session_lookup_dataset (s->parent, name)
          : NULL);
}

struct dataset *
session_lookup_dataset_assert (const struct session *s, const char *name)
{
  struct dataset *ds = session_lookup_dataset (s, name);
  assert (ds != NULL);
  return ds;
}

void
session_set_default_syntax_encoding (struct session *s, const char *encoding)
{
  free (s->syntax_encoding);
  s->syntax_encoding = xstrdup (encoding);
}

const char *
session_get_default_syntax_encoding (const struct session *s)
{
  return s->syntax_encoding;
}

size_t
session_n_datasets (const struct session *s)
{
  return hmapx_count (&s->datasets);
}

void
session_for_each_dataset (const struct session *s,
                          void (*cb) (struct dataset *, void *aux),
                          void *aux)
{
  struct hmapx_node *node, *next;
  struct dataset *ds;

  HMAPX_FOR_EACH_SAFE (ds, node, next, &s->datasets)
    cb (ds, aux);
}

struct dataset *
session_get_dataset_by_seqno (const struct session *s, unsigned int seqno)
{
  struct hmapx_node *node;
  struct dataset *ds;

  HMAPX_FOR_EACH (ds, node, &s->datasets)
    if (dataset_seqno (ds) == seqno)
      return ds;
  return NULL;
}

/* Returns an identifier that is is not currently in use as a dataset name.
   The caller must free the returned identifier, with free(). */
char *
session_generate_dataset_name (struct session *s)
{
  for (;;)
    {
      char *name;

      s->n_dataset_names++;
      assert(s->n_dataset_names != 0);

      name = xasprintf ("DataSet%u", s->n_dataset_names);
      if (!session_lookup_dataset (s, name))
        return name;
      free (name);
    }
}

static struct hmapx_node *
session_lookup_dataset__ (const struct session *s_, const char *name)
{
  struct session *s = CONST_CAST (struct session *, s_);
  struct hmapx_node *node;
  struct dataset *ds;

  HMAPX_FOR_EACH_WITH_HASH (ds, node, utf8_hash_case_string (name, 0),
                            &s->datasets)
    if (!utf8_strcasecmp (dataset_name (ds), name))
      return node;

  return NULL;
}
