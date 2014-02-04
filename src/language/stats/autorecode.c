/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2009, 2010, 2012, 2013, 2014 Free Software Foundation, Inc.

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

#include <float.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/casereader.h"
#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/transformations.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/compiler.h"
#include "libpspp/hash-functions.h"
#include "libpspp/hmap.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"
#include "gl/c-xvasprintf.h"
#include "gl/mbiter.h"


#include "gettext.h"
#define _(msgid) gettext (msgid)

/* FIXME: Implement PRINT subcommand. */

/* Explains how to recode one value. */
struct arc_item
  {
    struct hmap_node hmap_node; /* Element in "struct arc_spec" hash table. */
    union value from;           /* Original value. */
    int width;                  /* Width of the original value */

    double to;			/* Recoded value. */
  };

/* Explains how to recode an AUTORECODE variable. */
struct arc_spec
  {
    int width;                  /* Variable width. */
    int src_idx;                /* Case index of source variable. */
    struct variable *dst;	/* Target variable. */
    struct rec_items *items;
  };

/* Descending or ascending sort order. */
enum arc_direction
  {
    ASCENDING,
    DESCENDING
  };

struct rec_items
{
  struct hmap ht;         /* Hash table of "struct arc_item"s. */
  int refcnt;
};



/* AUTORECODE data. */
struct autorecode_pgm
{
  struct arc_spec *specs;
  size_t n_specs;

  /* Hash table of "struct arc_item"s. */
  struct rec_items *global_items;

  bool blank_valid;
};

static trns_proc_func autorecode_trns_proc;
static trns_free_func autorecode_trns_free;

static int compare_arc_items (const void *, const void *, const void *aux);
static void arc_free (struct autorecode_pgm *);
static struct arc_item *find_arc_item (const struct arc_spec *, const union value *,
                                       size_t hash);

static bool
value_is_blank (const union value *val, int width, const struct dictionary *dict)
{
  mbi_iterator_t iter;
  const char *str = CHAR_CAST_BUG (const char*, value_str (val, width));
  char *text = recode_string (UTF8, dict_get_encoding (dict), str, width);

  for (mbi_init (iter, text, width); mbi_avail (iter); mbi_advance (iter))
    {
      mbchar_t c = mbi_cur (iter);

      if ( ! mb_isblank (c))
	{
	  free (text);
	  return false;
	}
    }

  free (text);
  return true;
}

/* Performs the AUTORECODE procedure. */
int
cmd_autorecode (struct lexer *lexer, struct dataset *ds)
{
  struct autorecode_pgm *arc = NULL;

  struct dictionary *dict = dataset_dict (ds);
  const struct variable **src_vars = NULL;
  char **dst_names = NULL;
  size_t n_srcs = 0;
  size_t n_dsts = 0;

  enum arc_direction direction = ASCENDING;

  struct casereader *input;
  struct ccase *c;

  size_t i;
  bool ok;

  /* Create procedure. */
  arc = xzalloc (sizeof *arc);
  arc->blank_valid = true;

  /* Parse variable lists. */
  lex_match_id (lexer, "VARIABLES");
  lex_match (lexer, T_EQUALS);
  if (!parse_variables_const (lexer, dict, &src_vars, &n_srcs,
                              PV_NO_DUPLICATE | PV_NO_SCRATCH))
    goto error;
  if (!lex_force_match_id (lexer, "INTO"))
    goto error;
  lex_match (lexer, T_EQUALS);
  if (!parse_DATA_LIST_vars (lexer, dict, &dst_names, &n_dsts,
                             PV_NO_DUPLICATE))
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

      if (dict_lookup_var (dict, name) != NULL)
        {
          msg (SE, _("Target variable %s duplicates existing variable %s."),
               name, name);
          goto error;
        }
    }

  /* Parse options. */
  while (lex_match (lexer, T_SLASH))
    {
      if (lex_match_id (lexer, "DESCENDING"))
	direction = DESCENDING;
      else if (lex_match_id (lexer, "PRINT"))
        {
          /* Not yet implemented. */
        }
      else if (lex_match_id (lexer, "GROUP"))
	{
	  arc->global_items = xmalloc (sizeof (*arc->global_items));
	  arc->global_items->refcnt = 1;
	  hmap_init (&arc->global_items->ht);
	}
      else if (lex_match_id (lexer, "BLANK"))
	{
	  lex_match (lexer, T_EQUALS);
	  if (lex_match_id (lexer, "VALID"))
	    {
	      arc->blank_valid = true;
	    }
	  else if (lex_match_id (lexer, "MISSING"))
	    {
	      arc->blank_valid = false;
	    }
	  else
	    goto error;
	}
      else
	goto error;
    }

  if (lex_token (lexer) != T_ENDCMD)
    {
      lex_error (lexer, _("expecting end of command"));
      goto error;
    }

  arc->specs = xmalloc (n_dsts * sizeof *arc->specs);
  arc->n_specs = n_dsts;


  for (i = 0; i < n_dsts; i++)
    {
      struct arc_spec *spec = &arc->specs[i];

      spec->width = var_get_width (src_vars[i]);
      spec->src_idx = var_get_case_index (src_vars[i]);

      if (arc->global_items)
	{
	  arc->global_items->refcnt++;
	  spec->items = arc->global_items;
	}
      else
	{
	  spec->items = xzalloc (sizeof (*spec->items));
	  spec->items->refcnt = 1;
	  hmap_init (&spec->items->ht);
	}
    }


  /* Execute procedure. */
  input = proc_open (ds);
  for (; (c = casereader_read (input)) != NULL; case_unref (c))
    for (i = 0; i < arc->n_specs; i++)
      {
        struct arc_spec *spec = &arc->specs[i];
        int width = spec->width;
        const union value *value = case_data_idx (c, spec->src_idx);
        size_t hash = value_hash (value, width, 0);
        struct arc_item *item;

        item = find_arc_item (spec, value, hash);
        if ( (item == NULL)
	     &&  
	     ( arc->blank_valid
               || val_type_from_width (spec->width) == VAL_NUMERIC
               || ! value_is_blank (value, width, dict))
	     )
          {
            item = xmalloc (sizeof *item);
	    item->width = width;
            value_clone (&item->from, value, width);
            hmap_insert (&spec->items->ht, &item->hmap_node, hash);
          }
      }
  ok = casereader_destroy (input);
  ok = proc_commit (ds) && ok;

  /* Re-fetch dictionary because it might have changed (if TEMPORARY was in
     use). */
  dict = dataset_dict (ds);

  /* Create transformation. */
  for (i = 0; i < arc->n_specs; i++)
    {
      struct arc_spec *spec = &arc->specs[i];
      struct arc_item **items;
      struct arc_item *item;
      size_t n_items;
      size_t j;

      /* Create destination variable. */
      spec->dst = dict_create_var_assert (dict, dst_names[i], 0);

      /* Create array of pointers to items. */
      n_items = hmap_count (&spec->items->ht);
      items = xmalloc (n_items * sizeof *items);
      j = 0;
      HMAP_FOR_EACH (item, struct arc_item, hmap_node, &spec->items->ht)
        items[j++] = item;

      assert (j == n_items);

      /* Sort array by value. */
      sort (items, n_items, sizeof *items, compare_arc_items, NULL);

      /* Assign recoded values in sorted order. */
      for (j = 0; j < n_items; j++)
	{
	  const union value *from = &items[j]->from;
	  char *recoded_value  = NULL;
	  const int src_width = items[j]->width;
	  union value to_val;
	  size_t len;

	  value_init (&to_val, 0);

	  items[j]->to = direction == ASCENDING ? j + 1 : n_items - j;
	  
	  to_val.f = items[j]->to;

	  /* Add value labels to the destination variable which indicate
	     the source value from whence the new value comes. */
	  if (src_width > 0)
	    {
	      const char *str = CHAR_CAST_BUG (const char*, value_str (from, src_width));

	      recoded_value = recode_string (UTF8, dict_get_encoding (dict),
                                             str, src_width);
	    }
	  else
	    recoded_value = c_xasprintf ("%.*g", DBL_DIG + 1, from->f);
	  
	  /* Remove trailing whitespace */
          len = strlen (recoded_value);
          while (len > 0 && recoded_value[len - 1] == ' ')
            recoded_value[--len] = '\0';

	  var_add_value_label (spec->dst, &to_val, recoded_value);
	  value_destroy (&to_val, 0);
	  free (recoded_value);
	}

      /* Free array. */
      free (items);
    }
  add_transformation (ds, autorecode_trns_proc, autorecode_trns_free, arc);

  for (i = 0; i < n_dsts; i++)
    free (dst_names[i]);
  free (dst_names);
  free (src_vars);

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
          struct arc_item *item, *next;

	  HMAP_FOR_EACH_SAFE (item, next, struct arc_item, hmap_node,
			      &spec->items->ht)
	    {
	      value_destroy (&item->from, item->width);
	      hmap_delete (&spec->items->ht, &item->hmap_node);
	      free (item);
	    }
        }

      for (i = 0; i < arc->n_specs; i++)
	{
	  struct arc_spec *spec = &arc->specs[i];
	  
	  if (--spec->items->refcnt == 0)
	    {
	      hmap_destroy (&spec->items->ht);
	      free (spec->items);
	    }
	}

      if (arc->global_items && --arc->global_items->refcnt == 0)
	{
	  hmap_destroy (&arc->global_items->ht);
	  free (arc->global_items);
	}
      
      free (arc->specs);
      free (arc);
    }
}

static struct arc_item *
find_arc_item (const struct arc_spec *spec, const union value *value,
               size_t hash)
{
  struct arc_item *item;

  HMAP_FOR_EACH_WITH_HASH (item, struct arc_item, hmap_node, hash, &spec->items->ht)
    if (value_equal (value, &item->from, spec->width))
      return item;
  return NULL;
}

static int
compare_arc_items (const void *a_, const void *b_, const void *aux UNUSED)
{
  const struct arc_item *const *a = a_;
  const struct arc_item *const *b = b_;
  int width_a = (*a)->width;
  int width_b = (*b)->width;

  if ( width_a == width_b)
    return value_compare_3way (&(*a)->from, &(*b)->from, width_a);

  if ( width_a == 0 && width_b != 0)
    return -1;

  if ( width_b == 0 && width_a != 0)
    return +1;

  return buf_compare_rpad (CHAR_CAST_BUG (const char *, value_str (&(*a)->from, width_a)), width_a,
			   CHAR_CAST_BUG (const char *, value_str (&(*b)->from, width_b)), width_b);
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
      const struct arc_spec *spec = &arc->specs[i];
      const union value *value = case_data_idx (*c, spec->src_idx);
      unsigned int hash = value_hash (value, spec->width, 0);
      const struct arc_item *item = find_arc_item (spec, value, hash);

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
