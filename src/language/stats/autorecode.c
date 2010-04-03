/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dictionary.h"
#include "data/procedure.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* FIXME: Implement PRINT subcommand. */

/* Explains how to recode one value. */
struct arc_item
  {
    struct hmap_node hmap_node; /* Element in "struct arc_spec" hash table. */
    union value from;           /* Original value. */
    double to;			/* Recoded value. */
  };

/* Explains how to recode an AUTORECODE variable. */
struct arc_spec
  {
    const struct variable *src;	/* Source variable. */
    struct variable *dst;	/* Target variable. */
    struct hmap items;          /* Hash table of "struct arc_item"s. */
  };

/* Descending or ascending sort order. */
enum arc_direction
  {
    ASCENDING,
    DESCENDING
  };

/* AUTORECODE data. */
struct autorecode_pgm
  {
    struct arc_spec *specs;
    size_t n_specs;
  };

static trns_proc_func autorecode_trns_proc;
static trns_free_func autorecode_trns_free;

static int compare_arc_items (const void *, const void *, const void *width);
static void arc_free (struct autorecode_pgm *);
static struct arc_item *find_arc_item (struct arc_spec *, const union value *,
                                       size_t hash);

/* Performs the AUTORECODE procedure. */
int
cmd_autorecode (struct lexer *lexer, struct dataset *ds)
{
  struct autorecode_pgm *arc = NULL;

  const struct variable **src_vars = NULL;
  char **dst_names = NULL;
  size_t n_srcs = 0;
  size_t n_dsts = 0;

  enum arc_direction direction;
  bool print;

  struct casereader *input;
  struct ccase *c;

  size_t i;
  bool ok;

  /* Parse variable lists. */
  lex_match_id (lexer, "VARIABLES");
  lex_match (lexer, '=');
  if (!parse_variables_const (lexer, dataset_dict (ds), &src_vars, &n_srcs,
                              PV_NO_DUPLICATE))
    goto error;
  if (!lex_force_match_id (lexer, "INTO"))
    goto error;
  lex_match (lexer, '=');
  if (!parse_DATA_LIST_vars (lexer, &dst_names, &n_dsts, PV_NO_DUPLICATE))
    goto error;
  if (n_dsts != n_srcs)
    {
      msg (SE, _("Source variable count (%zu) does not match "
                 "target variable count (%zu)."),
           n_srcs, n_dsts);

      goto error;
    }
  for (i = 0; i < n_dsts; i++)
    {
      const char *name = dst_names[i];

      if (dict_lookup_var (dataset_dict (ds), name) != NULL)
        {
          msg (SE, _("Target variable %s duplicates existing variable %s."),
               name, name);
          goto error;
        }
    }

  /* Parse options. */
  direction = ASCENDING;
  print = false;
  while (lex_match (lexer, '/'))
    if (lex_match_id (lexer, "DESCENDING"))
      direction = DESCENDING;
    else if (lex_match_id (lexer, "PRINT"))
      print = true;
  if (lex_token (lexer) != '.')
    {
      lex_error (lexer, _("expecting end of command"));
      goto error;
    }

  /* Create procedure. */
  arc = xmalloc (sizeof *arc);
  arc->specs = xmalloc (n_dsts * sizeof *arc->specs);
  arc->n_specs = n_dsts;
  for (i = 0; i < n_dsts; i++)
    {
      struct arc_spec *spec = &arc->specs[i];

      spec->src = src_vars[i];
      hmap_init (&spec->items);
    }

  /* Execute procedure. */
  input = proc_open (ds);
  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    for (i = 0; i < arc->n_specs; i++)
      {
        struct arc_spec *spec = &arc->specs[i];
        int width = var_get_width (spec->src);
        const union value *value = case_data (c, spec->src);
        size_t hash = value_hash (value, width, 0);
        struct arc_item *item;

        item = find_arc_item (spec, value, hash);
        if (item == NULL)
          {
            item = xmalloc (sizeof *item);
            value_clone (&item->from, value, width);
            hmap_insert (&spec->items, &item->hmap_node, hash);
          }
      }
  ok = casereader_destroy (input);
  ok = proc_commit (ds) && ok;

  /* Create transformation. */
  for (i = 0; i < arc->n_specs; i++)
    {
      struct arc_spec *spec = &arc->specs[i];
      struct arc_item **items;
      struct arc_item *item;
      size_t n_items;
      int src_width;
      size_t j;

      /* Create destination variable. */
      spec->dst = dict_create_var_assert (dataset_dict (ds), dst_names[i], 0);

      /* Create array of pointers to items. */
      n_items = hmap_count (&spec->items);
      items = xmalloc (n_items * sizeof *items);
      j = 0;
      HMAP_FOR_EACH (item, struct arc_item, hmap_node, &spec->items)
        items[j++] = item;
      assert (j == n_items);

      /* Sort array by value. */
      src_width = var_get_width (spec->src);
      sort (items, n_items, sizeof *items, compare_arc_items, &src_width);

      /* Assign recoded values in sorted order. */
      for (j = 0; j < n_items; j++)
        items[j]->to = direction == ASCENDING ? j + 1 : n_items - j;

      /* Free array. */
      free (items);
    }
  add_transformation (ds, autorecode_trns_proc, autorecode_trns_free, arc);

  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

error:
  for (i = 0; i < n_dsts; i++)
    free (dst_names[i]);
  free (dst_names);
  free (src_vars);
  arc_free (arc);
  return CMD_CASCADING_FAILURE;
}

static void
arc_free (struct autorecode_pgm *arc)
{
  if (arc != NULL)
    {
      size_t i;

      for (i = 0; i < arc->n_specs; i++)
        {
          struct arc_spec *spec = &arc->specs[i];
          int width = var_get_width (spec->src);
          struct arc_item *item, *next;

          HMAP_FOR_EACH_SAFE (item, next, struct arc_item, hmap_node,
                              &spec->items)
            {
              value_destroy (&item->from, width);
              hmap_delete (&spec->items, &item->hmap_node);
              free (item);
            }
          hmap_destroy (&spec->items);
        }
      free (arc->specs);
      free (arc);
    }
}

static struct arc_item *
find_arc_item (struct arc_spec *spec, const union value *value,
               size_t hash)
{
  struct arc_item *item;

  HMAP_FOR_EACH_WITH_HASH (item, struct arc_item, hmap_node, hash,
                           &spec->items)
    if (value_equal (value, &item->from, var_get_width (spec->src)))
      return item;
  return NULL;
}

static int
compare_arc_items (const void *a_, const void *b_, const void *width_)
{
  const struct arc_item *const *a = a_;
  const struct arc_item *const *b = b_;
  const int *width = width_;

  return value_compare_3way (&(*a)->from, &(*b)->from, *width);
}

static int
autorecode_trns_proc (void *arc_, struct ccase **c,
                      casenumber case_idx UNUSED)
{
  struct autorecode_pgm *arc = arc_;
  size_t i;

  *c = case_unshare (*c);
  for (i = 0; i < arc->n_specs; i++)
    {
      struct arc_spec *spec = &arc->specs[i];
      int width = var_get_width (spec->src);
      const union value *value = case_data (*c, spec->src);
      struct arc_item *item;

      item = find_arc_item (spec, value, value_hash (value, width, 0));
      case_data_rw (*c, spec->dst)->f = item ? item->to : SYSMIS;
    }

  return TRNS_CONTINUE;
}

static bool
autorecode_trns_free (void *arc_)
{
  struct autorecode_pgm *arc = arc_;

  arc_free (arc);
  return true;
}
