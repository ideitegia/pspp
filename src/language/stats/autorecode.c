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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include <stdlib.h>

#include <data/case.h>
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/transformations.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* FIXME: Implement PRINT subcommand. */

/* An AUTORECODE variable's original value. */
union arc_value 
  {
    double f;                   /* Numeric. */
    char *c;                    /* Short or long string. */
  };

/* Explains how to recode one value.  `from' must be first element.  */
struct arc_item
  {
    union arc_value from;	/* Original value. */
    double to;			/* Recoded value. */
  };

/* Explains how to recode an AUTORECODE variable. */
struct arc_spec
  {
    struct variable *src;	/* Source variable. */
    struct variable *dest;	/* Target variable. */
    struct hsh_table *items;	/* Hash table of `freq's. */
  };

/* AUTORECODE transformation. */
struct autorecode_trns
  {
    struct pool *pool;		/* Contains AUTORECODE specs. */
    struct arc_spec *specs;	/* AUTORECODE specifications. */
    size_t spec_cnt;		/* Number of specifications. */
  };

/* Descending or ascending sort order. */
enum direction 
  {
    ASCENDING,
    DESCENDING
  };

/* AUTORECODE data. */
struct autorecode_pgm 
  {
    struct variable **src_vars;    /* Source variables. */
    char **dst_names;              /* Target variable names. */
    struct variable **dst_vars;    /* Target variables. */
    struct hsh_table **src_values; /* `union arc_value's of source vars. */
    size_t var_cnt;                /* Number of variables. */
    struct pool *src_values_pool;  /* Pool used by src_values. */
    enum direction direction;      /* Sort order. */
    int print;                     /* Print mapping table if nonzero. */
  };

static trns_proc_func autorecode_trns_proc;
static trns_free_func autorecode_trns_free;
static bool autorecode_proc_func (const struct ccase *, void *, const struct dataset *);
static hsh_compare_func compare_alpha_value, compare_numeric_value;
static hsh_hash_func hash_alpha_value, hash_numeric_value;

static void recode (struct dataset *, const struct autorecode_pgm *);
static void arc_free (struct autorecode_pgm *);

/* Performs the AUTORECODE procedure. */
int
cmd_autorecode (struct lexer *lexer, struct dataset *ds)
{
  struct autorecode_pgm arc;
  size_t dst_cnt;
  size_t i;
  bool ok;

  arc.src_vars = NULL;
  arc.dst_names = NULL;
  arc.dst_vars = NULL;
  arc.src_values = NULL;
  arc.var_cnt = 0;
  arc.src_values_pool = NULL;
  arc.direction = ASCENDING;
  arc.print = 0;
  dst_cnt = 0;

  lex_match_id (lexer, "VARIABLES");
  lex_match (lexer, '=');
  if (!parse_variables (lexer, dataset_dict (ds), &arc.src_vars, &arc.var_cnt,
                        PV_NO_DUPLICATE))
    goto lossage;
  if (!lex_force_match_id (lexer, "INTO"))
    goto lossage;
  lex_match (lexer, '=');
  if (!parse_DATA_LIST_vars (lexer, &arc.dst_names, &dst_cnt, PV_NONE))
    goto lossage;
  if (dst_cnt != arc.var_cnt)
    {
      size_t i;

      msg (SE, _("Source variable count (%u) does not match "
                 "target variable count (%u)."),
           (unsigned) arc.var_cnt, (unsigned) dst_cnt);

      for (i = 0; i < dst_cnt; i++)
        free (arc.dst_names[i]);
      free (arc.dst_names);
      arc.dst_names = NULL;

      goto lossage;
    }
  while (lex_match (lexer, '/'))
    if (lex_match_id (lexer, "DESCENDING"))
      arc.direction = DESCENDING;
    else if (lex_match_id (lexer, "PRINT"))
      arc.print = 1;
  if (lex_token (lexer) != '.')
    {
      lex_error (lexer, _("expecting end of command"));
      goto lossage;
    }

  for (i = 0; i < arc.var_cnt; i++)
    {
      int j;

      if (dict_lookup_var (dataset_dict (ds), arc.dst_names[i]) != NULL)
	{
	  msg (SE, _("Target variable %s duplicates existing variable %s."),
	       arc.dst_names[i], arc.dst_names[i]);
	  goto lossage;
	}
      for (j = 0; j < i; j++)
	if (!strcasecmp (arc.dst_names[i], arc.dst_names[j]))
	  {
	    msg (SE, _("Duplicate variable name %s among target variables."),
		 arc.dst_names[i]);
	    goto lossage;
	  }
    }

  arc.src_values_pool = pool_create ();
  arc.dst_vars = xnmalloc (arc.var_cnt, sizeof *arc.dst_vars);
  arc.src_values = xnmalloc (arc.var_cnt, sizeof *arc.src_values);
  for (i = 0; i < dst_cnt; i++)
    if (arc.src_vars[i]->type == ALPHA)
      arc.src_values[i] = hsh_create (10, compare_alpha_value,
                                      hash_alpha_value, NULL, arc.src_vars[i]);
    else
      arc.src_values[i] = hsh_create (10, compare_numeric_value,
                                      hash_numeric_value, NULL, NULL);

  ok = procedure (ds, autorecode_proc_func, &arc);

  for (i = 0; i < arc.var_cnt; i++)
    arc.dst_vars[i] = dict_create_var_assert (dataset_dict (ds),
                                              arc.dst_names[i], 0);

  recode (ds, &arc);
  arc_free (&arc);
  return ok ? CMD_SUCCESS : CMD_CASCADING_FAILURE;

lossage:
  arc_free (&arc);
  return CMD_CASCADING_FAILURE;
}

static void
arc_free (struct autorecode_pgm *arc) 
{
  free (arc->src_vars);
  if (arc->dst_names != NULL) 
    {
      size_t i;
      
      for (i = 0; i < arc->var_cnt; i++)
        free (arc->dst_names[i]);
      free (arc->dst_names);
    }
  free (arc->dst_vars);
  if (arc->src_values != NULL) 
    {
      size_t i;

      for (i = 0; i < arc->var_cnt; i++)
        hsh_destroy (arc->src_values[i]);
      free (arc->src_values);
    }
  pool_destroy (arc->src_values_pool);
}


/* AUTORECODE transformation. */

static void
recode (struct dataset *ds, const struct autorecode_pgm *arc)
{
  struct autorecode_trns *trns;
  size_t i;

  trns = pool_create_container (struct autorecode_trns, pool);
  trns->specs = pool_nalloc (trns->pool, arc->var_cnt, sizeof *trns->specs);
  trns->spec_cnt = arc->var_cnt;
  for (i = 0; i < arc->var_cnt; i++)
    {
      struct arc_spec *spec = &trns->specs[i];
      void *const *p = hsh_sort (arc->src_values[i]);
      int count = hsh_count (arc->src_values[i]);
      int j;

      spec->src = arc->src_vars[i];
      spec->dest = arc->dst_vars[i];

      if (arc->src_vars[i]->type == ALPHA)
	spec->items = hsh_create (2 * count, compare_alpha_value,
				  hash_alpha_value, NULL, arc->src_vars[i]);
      else
	spec->items = hsh_create (2 * count, compare_numeric_value,
				  hash_numeric_value, NULL, NULL);

      for (j = 0; *p; p++, j++)
	{
	  struct arc_item *item = pool_alloc (trns->pool, sizeof *item);
          union arc_value *vp = *p;
          
	  if (arc->src_vars[i]->type == NUMERIC)
            item->from.f = vp->f;
          else
	    item->from.c = pool_clone (trns->pool, vp->c,
                                       arc->src_vars[i]->width);
	  item->to = arc->direction == ASCENDING ? j + 1 : count - j;
	  hsh_force_insert (spec->items, item);
	}
    }
  add_transformation (ds, 
		      autorecode_trns_proc, autorecode_trns_free, trns);
}

/* Executes an AUTORECODE transformation. */
static int
autorecode_trns_proc (void *trns_, struct ccase *c, casenumber case_idx UNUSED)
{
  struct autorecode_trns *trns = trns_;
  size_t i;

  for (i = 0; i < trns->spec_cnt; i++)
    {
      struct arc_spec *spec = &trns->specs[i];
      struct arc_item *item;
      union arc_value v;

      if (spec->src->type == NUMERIC)
        v.f = case_num (c, spec->src->fv);
      else
        v.c = (char *) case_str (c, spec->src->fv);
      item = hsh_force_find (spec->items, &v);

      case_data_rw (c, spec->dest->fv)->f = item->to;
    }
  return TRNS_CONTINUE;
}

/* Frees an AUTORECODE transformation. */
static bool
autorecode_trns_free (void *trns_)
{
  struct autorecode_trns *trns = trns_;
  size_t i;

  for (i = 0; i < trns->spec_cnt; i++)
    hsh_destroy (trns->specs[i].items);
  pool_destroy (trns->pool);
  return true;
}

/* AUTORECODE procedure. */

static int
compare_alpha_value (const void *a_, const void *b_, const void *v_)
{
  const union arc_value *a = a_;
  const union arc_value *b = b_;
  const struct variable *v = v_;

  return memcmp (a->c, b->c, v->width);
}

static unsigned
hash_alpha_value (const void *a_, const void *v_)
{
  const union arc_value *a = a_;
  const struct variable *v = v_;
  
  return hsh_hash_bytes (a->c, v->width);
}

static int
compare_numeric_value (const void *a_, const void *b_, const void *aux UNUSED)
{
  const union arc_value *a = a_;
  const union arc_value *b = b_;

  return a->f < b->f ? -1 : a->f > b->f;
}

static unsigned
hash_numeric_value (const void *a_, const void *aux UNUSED)
{
  const union arc_value *a = a_;

  return hsh_hash_double (a->f);
}

static bool
autorecode_proc_func (const struct ccase *c, void *arc_, const struct dataset *ds UNUSED)
{
  struct autorecode_pgm *arc = arc_;
  size_t i;

  for (i = 0; i < arc->var_cnt; i++)
    {
      union arc_value v, *vp, **vpp;

      if (arc->src_vars[i]->type == NUMERIC)
        v.f = case_num (c, arc->src_vars[i]->fv);
      else
        v.c = (char *) case_str (c, arc->src_vars[i]->fv);

      vpp = (union arc_value **) hsh_probe (arc->src_values[i], &v);
      if (*vpp == NULL)
        {
          vp = pool_alloc (arc->src_values_pool, sizeof *vp);
          if (arc->src_vars[i]->type == NUMERIC)
            vp->f = v.f;
          else
            vp->c = pool_clone (arc->src_values_pool,
                                v.c, arc->src_vars[i]->width);
          *vpp = vp;
        }
    }
  return true;
}
