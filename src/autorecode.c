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
#include "error.h"
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "pool.h"
#include "str.h"
#include "var.h"
#include "vfm.h"

/* FIXME: Implement PRINT subcommand. */

/* Explains how to recode one value.  `from' must be first element.  */
struct arc_item
  {
    union value from;		/* Original value. */
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
    struct trns_header h;
    struct pool *owner;		/* Contains AUTORECODE specs. */
    struct arc_spec *specs;	/* AUTORECODE specifications. */
    int spec_cnt;		/* Number of specifications. */
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
    struct hsh_table **src_values; /* `union value's of source vars. */
    int var_cnt;                   /* Number of variables. */
    struct pool *src_values_pool;  /* Pool used by src_values. */
    enum direction direction;      /* Sort order. */
    int print;                     /* Print mapping table if nonzero. */
  };

static trns_proc_func autorecode_trns_proc;
static trns_free_func autorecode_trns_free;
static int autorecode_proc_func (struct ccase *, void *);
static hsh_compare_func compare_alpha_value, compare_numeric_value;
static hsh_hash_func hash_alpha_value, hash_numeric_value;

static void recode (const struct autorecode_pgm *);
static void arc_free (struct autorecode_pgm *);

/* Performs the AUTORECODE procedure. */
int
cmd_autorecode (void)
{
  struct autorecode_pgm arc;
  int dst_cnt;
  int i;

  arc.src_vars = NULL;
  arc.dst_names = NULL;
  arc.dst_vars = NULL;
  arc.src_values = NULL;
  arc.var_cnt = 0;
  arc.src_values_pool = NULL;
  arc.direction = ASCENDING;
  arc.print = 0;
  dst_cnt = 0;

  lex_match_id ("VARIABLES");
  lex_match ('=');
  if (!parse_variables (default_dict, &arc.src_vars, &arc.var_cnt,
                        PV_NO_DUPLICATE))
    goto lossage;
  if (!lex_force_match_id ("INTO"))
    goto lossage;
  lex_match ('=');
  if (!parse_DATA_LIST_vars (&arc.dst_names, &dst_cnt, PV_NONE))
    goto lossage;
  if (dst_cnt != arc.var_cnt)
    {
      int i;

      msg (SE, _("Source variable count (%d) does not match "
                 "target variable count (%d)."), arc.var_cnt, dst_cnt);

      for (i = 0; i < dst_cnt; i++)
        free (arc.dst_names[i]);
      free (arc.dst_names);
      arc.dst_names = NULL;

      goto lossage;
    }
  while (lex_match ('/'))
    if (lex_match_id ("DESCENDING"))
      arc.direction = DESCENDING;
    else if (lex_match_id ("PRINT"))
      arc.print = 1;
  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      goto lossage;
    }

  for (i = 0; i < arc.var_cnt; i++)
    {
      int j;

      if (dict_lookup_var (default_dict, arc.dst_names[i]) != NULL)
	{
	  msg (SE, _("Target variable %s duplicates existing variable %s."),
	       arc.dst_names[i], arc.dst_names[i]);
	  goto lossage;
	}
      for (j = 0; j < i; j++)
	if (!strcmp (arc.dst_names[i], arc.dst_names[j]))
	  {
	    msg (SE, _("Duplicate variable name %s among target variables."),
		 arc.dst_names[i]);
	    goto lossage;
	  }
    }

  arc.src_values_pool = pool_create ();
  arc.dst_vars = xmalloc (sizeof *arc.dst_vars * arc.var_cnt);
  arc.src_values = xmalloc (sizeof *arc.src_values * arc.var_cnt);
  for (i = 0; i < dst_cnt; i++)
    if (arc.src_vars[i]->type == ALPHA)
      arc.src_values[i] = hsh_create (10, compare_alpha_value,
                                      hash_alpha_value, NULL, arc.src_vars[i]);
    else
      arc.src_values[i] = hsh_create (10, compare_numeric_value,
                                      hash_numeric_value, NULL, NULL);

  procedure (autorecode_proc_func, &arc);

  for (i = 0; i < arc.var_cnt; i++)
    {
      arc.dst_vars[i] = dict_create_var_assert (default_dict,
                                                arc.dst_names[i], 0);
      arc.dst_vars[i]->init = 0;
    }

  recode (&arc);
  arc_free (&arc);
  return CMD_SUCCESS;

lossage:
  arc_free (&arc);
  return CMD_FAILURE;
}

static void
arc_free (struct autorecode_pgm *arc) 
{
  free (arc->src_vars);
  if (arc->dst_names != NULL) 
    {
      int i;
      
      for (i = 0; i < arc->var_cnt; i++)
        free (arc->dst_names[i]);
      free (arc->dst_names);
    }
  free (arc->dst_vars);
  if (arc->src_values != NULL) 
    {
      int i;

      for (i = 0; i < arc->var_cnt; i++)
        hsh_destroy (arc->src_values[i]);
      free (arc->src_values);
    }
  pool_destroy (arc->src_values_pool);
}


/* AUTORECODE transformation. */

static void
recode (const struct autorecode_pgm *arc)
{
  struct autorecode_trns *t;
  struct pool *pool;
  int i;

  pool = pool_create ();
  t = xmalloc (sizeof *t);
  t->h.proc = autorecode_trns_proc;
  t->h.free = autorecode_trns_free;
  t->owner = pool;
  t->specs = pool_alloc (t->owner, sizeof *t->specs * arc->var_cnt);
  t->spec_cnt = arc->var_cnt;
  for (i = 0; i < arc->var_cnt; i++)
    {
      struct arc_spec *spec = &t->specs[i];
      void **p = hsh_sort (arc->src_values[i]);
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
	  struct arc_item *item = pool_alloc (t->owner, sizeof *item);
          union value *vp = *p;
          
	  if (arc->src_vars[i]->type == NUMERIC)
            item->from.f = vp->f;
          else
	    item->from.c = pool_strdup (t->owner, vp->c);
	  item->to = arc->direction == ASCENDING ? j + 1 : count - j;
	  hsh_force_insert (spec->items, item);
	}
    }
  add_transformation (&t->h);
}

static int
autorecode_trns_proc (struct trns_header * trns, struct ccase * c,
                      int case_num UNUSED)
{
  struct autorecode_trns *t = (struct autorecode_trns *) trns;
  int i;

  for (i = 0; i < t->spec_cnt; i++)
    {
      struct arc_spec *spec = &t->specs[i];
      struct arc_item *item;

      if (spec->src->type == NUMERIC)
	item = hsh_force_find (spec->items, &c->data[spec->src->fv].f);
      else
	{
	  union value v;
	  v.c = c->data[spec->src->fv].s;
	  item = hsh_force_find (spec->items, &v);
	}

      c->data[spec->dest->fv].f = item->to;
    }
  return -1;
}

static void
autorecode_trns_free (struct trns_header * trns)
{
  struct autorecode_trns *t = (struct autorecode_trns *) trns;
  int i;

  for (i = 0; i < t->spec_cnt; i++)
    hsh_destroy (t->specs[i].items);
  pool_destroy (t->owner);
}

/* AUTORECODE procedure. */

static int
compare_alpha_value (const void *a_, const void *b_, void *v_)
{
  const union value *a = a_;
  const union value *b = b_;
  const struct variable *v = v_;

  return memcmp (a->c, b->c, v->width);
}

static unsigned
hash_alpha_value (const void *a_, void *v_)
{
  const union value *a = a_;
  const struct variable *v = v_;
  
  return hsh_hash_bytes (a->c, v->width);
}

static int
compare_numeric_value (const void *a_, const void *b_, void *foo UNUSED)
{
  const union value *a = a_;
  const union value *b = b_;

  return a->f < b->f ? -1 : a->f > b->f;
}

static unsigned
hash_numeric_value (const void *a_, void *foo UNUSED)
{
  const union value *a = a_;

  return hsh_hash_double (a->f);
}

static int
autorecode_proc_func (struct ccase *c, void *arc_)
{
  struct autorecode_pgm *arc = arc_;
  int i;

  for (i = 0; i < arc->var_cnt; i++)
    {
      union value v, *vp, **vpp;

      if (arc->src_vars[i]->type == NUMERIC)
        v.f = c->data[arc->src_vars[i]->fv].f;
      else
        v.c = c->data[arc->src_vars[i]->fv].s;

      vpp = (union value **) hsh_probe (arc->src_values[i], &v);
      if (*vpp == NULL)
        {
          vp = pool_alloc (arc->src_values_pool, sizeof (union value));
          if (arc->src_vars[i]->type == NUMERIC)
            vp->f = v.f;
          else
            vp->c = pool_strndup (arc->src_values_pool,
                                  v.c, arc->src_vars[i]->width);
          *vpp = vp;
        }
    }
  return 1;
}
