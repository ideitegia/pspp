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
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "pool.h"
#include "str.h"
#include "var.h"
#include "vfm.h"

#include "debug-print.h"

/* FIXME: This module is less than ideally efficient, both in space
   and time.  If anyone cares, it would be a good project. */

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
    struct arc_spec *arc;	/* AUTORECODE specifications. */
    int n_arc;			/* Number of specifications. */
  };

/* Source and target variables, hash table translator. */
static struct variable **v_src;
static struct variable **v_dest;
static struct hsh_table **h_trans;
static int nv_src;

/* Pool for allocation of hash table entries. */
static struct pool *hash_pool;

/* Options. */
static int descend;
static int print;

static trns_proc_func autorecode_trns_proc;
static trns_free_func autorecode_trns_free;
static int autorecode_proc_func (struct ccase *, void *);
static hsh_compare_func compare_alpha_value, compare_numeric_value;
static hsh_hash_func hash_alpha_value, hash_numeric_value;
static void recode (void);

/* Performs the AUTORECODE procedure. */
int
cmd_autorecode (void)
{
  /* Dest var names. */
  char **n_dest = NULL;
  int nv_dest = 0;

  int i;

  v_src = NULL;
  descend = print = 0;
  h_trans = NULL;

  lex_match_id ("AUTORECODE");
  lex_match_id ("VARIABLES");
  lex_match ('=');
  if (!parse_variables (default_dict, &v_src, &nv_src, PV_NO_DUPLICATE))
    return CMD_FAILURE;
  if (!lex_force_match_id ("INTO"))
    return CMD_FAILURE;
  lex_match ('=');
  if (!parse_DATA_LIST_vars (&n_dest, &nv_dest, PV_NONE))
    goto lossage;
  if (nv_dest != nv_src)
    {
      msg (SE, _("Number of source variables (%d) does not match number "
	   "of target variables (%d)."), nv_src, nv_dest);
      goto lossage;
    }
  while (lex_match ('/'))
    if (lex_match_id ("DESCENDING"))
      descend = 1;
    else if (lex_match_id ("PRINT"))
      print = 1;
  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      goto lossage;
    }

  for (i = 0; i < nv_dest; i++)
    {
      int j;

      if (dict_lookup_var (default_dict, n_dest[i]) != NULL)
	{
	  msg (SE, _("Target variable %s duplicates existing variable %s."),
	       n_dest[i], n_dest[i]);
	  goto lossage;
	}
      for (j = 0; j < i; j++)
	if (!strcmp (n_dest[i], n_dest[j]))
	  {
	    msg (SE, _("Duplicate variable name %s among target variables."),
		 n_dest[i]);
	    goto lossage;
	  }
    }

  hash_pool = pool_create ();

  v_dest = xmalloc (sizeof *v_dest * nv_dest);
  h_trans = xmalloc (sizeof *h_trans * nv_dest);
  for (i = 0; i < nv_dest; i++)
    if (v_src[i]->type == ALPHA)
      h_trans[i] = hsh_create (10, compare_alpha_value,
			       hash_alpha_value, NULL, v_src[i]);
    else
      h_trans[i] = hsh_create (10, compare_numeric_value,
			       hash_numeric_value, NULL, NULL);

  procedure_with_splits (NULL, autorecode_proc_func, NULL, NULL);

  for (i = 0; i < nv_dest; i++)
    {
      v_dest[i] = dict_create_var_assert (default_dict, n_dest[i], 0);
      v_dest[i]->init = 0;
      free (n_dest[i]);
    }
  free (n_dest);

  recode ();
  
  free (v_src);
  free (v_dest);

  return CMD_SUCCESS;

lossage:
  if (h_trans != NULL)
    for (i = 0; i < nv_src; i++)
      hsh_destroy (h_trans[i]);
  for (i = 0; i < nv_dest; i++)
    free (n_dest[i]);
  free (n_dest);
  free (v_src);
  return CMD_FAILURE;
}

/* AUTORECODE transformation. */

static void
recode (void)
{
  struct autorecode_trns *t;
  struct pool *arc_pool;
  int i;

  arc_pool = pool_create ();
  t = xmalloc (sizeof *t);
  t->h.proc = autorecode_trns_proc;
  t->h.free = autorecode_trns_free;
  t->owner = arc_pool;
  t->arc = pool_alloc (arc_pool, sizeof *t->arc * nv_src);
  t->n_arc = nv_src;
  for (i = 0; i < nv_src; i++)
    {
      struct arc_spec *spec = &t->arc[i];
      void **p = hsh_sort (h_trans[i]);
      int count = hsh_count (h_trans[i]);
      int j;

      spec->src = v_src[i];
      spec->dest = v_dest[i];

      if (v_src[i]->type == ALPHA)
	spec->items = hsh_create (2 * count, compare_alpha_value,
				  hash_alpha_value, NULL, v_src[i]);
      else
	spec->items = hsh_create (2 * count, compare_numeric_value,
				  hash_numeric_value, NULL, NULL);

      for (j = 0; *p; p++, j++)
	{
	  struct arc_item *item = pool_alloc (arc_pool, sizeof *item);
          union value *vp = *p;
          
	  if (v_src[i]->type == NUMERIC)
            item->from.f = vp->f;
          else
	    item->from.c = pool_strdup (arc_pool, vp->c);
	  item->to = !descend ? j + 1 : count - j;
	  hsh_force_insert (spec->items, item);
	}
      
      hsh_destroy (h_trans[i]);
    }
  free (h_trans);
  pool_destroy (hash_pool);
  add_transformation ((struct trns_header *) t);
}

static int
autorecode_trns_proc (struct trns_header * trns, struct ccase * c,
                      int case_num UNUSED)
{
  struct autorecode_trns *t = (struct autorecode_trns *) trns;
  int i;

  for (i = 0; i < t->n_arc; i++)
    {
      struct arc_spec *spec = &t->arc[i];
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

  for (i = 0; i < t->n_arc; i++)
    hsh_destroy (t->arc[i].items);
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
autorecode_proc_func (struct ccase *c, void *aux UNUSED)
{
  int i;

  for (i = 0; i < nv_src; i++)
    {
      union value v;
      union value *vp;
      union value **vpp;

      if (v_src[i]->type == NUMERIC)
	{
	  v.f = c->data[v_src[i]->fv].f;
	  vpp = (union value **) hsh_probe (h_trans[i], &v);
	  if (*vpp == NULL)
	    {
	      vp = pool_alloc (hash_pool, sizeof (union value));
	      vp->f = v.f;
	      *vpp = vp;
	    }
	}
      else
	{
	  v.c = c->data[v_src[i]->fv].s;
	  vpp = (union value **) hsh_probe (h_trans[i], &v);
	  if (*vpp == NULL)
	    {
	      vp = pool_alloc (hash_pool, sizeof (union value));
	      vp->c = pool_strndup (hash_pool, v.c, v_src[i]->width);
	      *vpp = vp;
	    }
	}
    }
  return 1;
}
