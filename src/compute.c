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
#include <stdlib.h>
#include "alloc.h"
#include "approx.h"
#include "cases.h"
#include "command.h"
#include "error.h"
#include "expr.h"
#include "lexer.h"
#include "str.h"
#include "var.h"
#include "vector.h"

/* I can't think of any really good reason to disable debugging for
   this module. */
#include "debug-print.h"

/* COMPUTE and IF transformation. */
struct compute_trns
  {
    struct trns_header h;

    /* Destination.  (Used only during parsing.) */
    struct variable *v;		/* Destvar, if dest isn't a vector elem. */
    int created;		/* Whether we created the destvar (used only during
				   parsing). */

    /* Destination.  (Used during execution.) */
    struct vector *vec;		/* Destination vector, if dest is a vector elem. */
    int fv;			/* `value' index of destination variable. */
    int width;			/* Target variable width (string vars only). */

    /* Expressions. */
    struct expression *vec_elem;		/* Destination vector element expr. */
    struct expression *target;			/* Target expression. */
    struct expression *test;			/* Test expression (IF only). */
  };

static int parse_target_expression (struct compute_trns *,
				    int (*func_tab[4]) (struct trns_header *, struct ccase *));
static struct compute_trns *new_trns (void);
static void delete_trns (struct compute_trns *);
static void free_trns (struct trns_header *);
static int parse_var_or_vec (struct compute_trns *);

/* COMPUTE. */

static int compute_num (struct trns_header *, struct ccase *);
static int compute_str (struct trns_header *, struct ccase *);
static int compute_num_vec (struct trns_header *, struct ccase *);
static int compute_str_vec (struct trns_header *, struct ccase *);

int
cmd_compute (void)
{
  /* Table of functions to process data. */
  static int (*func_tab[4]) (struct trns_header *, struct ccase *) =
    {
      compute_num,
      compute_str,
      compute_num_vec,
      compute_str_vec,
    };

  /* Transformation being constructed. */
  struct compute_trns *c;

  lex_match_id ("COMPUTE");

  c = new_trns ();
  if (!parse_var_or_vec (c))
    goto fail;

  if (!lex_force_match ('=')
      || !parse_target_expression (c, func_tab))
    goto fail;

  /* Goofy behavior, but compatible: Turn off LEAVE on the destvar. */
  if (c->v && c->v->left && c->v->name[0] != '#')
    {
      devector (c->v);
      c->v->left = 0;
      envector (c->v);
    }

  add_transformation ((struct trns_header *) c);

  return CMD_SUCCESS;

fail:
  delete_trns (c);
  return CMD_FAILURE;
}

static int
compute_num (struct trns_header * pt, struct ccase * c)
{
  struct compute_trns *t = (struct compute_trns *) pt;
  expr_evaluate (t->target, c, &c->data[t->fv]);
  return -1;
}

static int
compute_num_vec (struct trns_header * pt, struct ccase * c)
{
  struct compute_trns *t = (struct compute_trns *) pt;

  /* Index into the vector. */
  union value index;

  /* Rounded index value. */
  int rindx;

  expr_evaluate (t->vec_elem, c, &index);
  rindx = floor (index.f + EPSILON);
  if (index.f == SYSMIS || rindx < 1 || rindx > t->vec->nv)
    {
      if (index.f == SYSMIS)
	msg (SW, _("When executing COMPUTE: SYSMIS is not a valid value as "
	     "an index into vector %s."), t->vec->name);
      else
	msg (SW, _("When executing COMPUTE: %g is not a valid value as "
	     "an index into vector %s."), index.f, t->vec->name);
      return -1;
    }
  expr_evaluate (t->target, c, &c->data[t->vec->v[rindx - 1]->fv]);
  return -1;
}

static int
compute_str (struct trns_header * pt, struct ccase * c)
{
  struct compute_trns *t = (struct compute_trns *) pt;

  /* Temporary storage for string expression return value. */
  union value v;

  expr_evaluate (t->target, c, &v);
  st_bare_pad_len_copy (c->data[t->fv].s, &v.c[1], t->width, v.c[0]);
  return -1;
}

static int
compute_str_vec (struct trns_header * pt, struct ccase * c)
{
  struct compute_trns *t = (struct compute_trns *) pt;

  /* Temporary storage for string expression return value. */
  union value v;

  /* Index into the vector. */
  union value index;

  /* Rounded index value. */
  int rindx;

  /* Variable reference by indexed vector. */
  struct variable *vr;

  expr_evaluate (t->vec_elem, c, &index);
  rindx = floor (index.f + EPSILON);
  if (index.f == SYSMIS || rindx < 1 || rindx > t->vec->nv)
    {
      if (index.f == SYSMIS)
	msg (SW, _("When executing COMPUTE: SYSMIS is not a valid value as "
	     "an index into vector %s."), t->vec->name);
      else
	msg (SW, _("When executing COMPUTE: %g is not a valid value as "
	     "an index into vector %s."), index.f, t->vec->name);
      return -1;
    }

  expr_evaluate (t->target, c, &v);
  vr = t->vec->v[rindx - 1];
  st_bare_pad_len_copy (c->data[vr->fv].s, &v.c[1], vr->width, v.c[0]);
  return -1;
}

/* IF. */

static int if_num (struct trns_header *, struct ccase *);
static int if_str (struct trns_header *, struct ccase *);
static int if_num_vec (struct trns_header *, struct ccase *);
static int if_str_vec (struct trns_header *, struct ccase *);

int
cmd_if (void)
{
  /* Table of functions to process data. */
  static int (*func_tab[4]) (struct trns_header *, struct ccase *) =
    {
      if_num,
      if_str,
      if_num_vec,
      if_str_vec,
    };

  /* Transformation being constructed. */
  struct compute_trns *c;

  lex_match_id ("IF");
  c = new_trns ();

  /* Test expression. */
  c->test = expr_parse (PXP_BOOLEAN);
  if (!c->test)
    goto fail;

  /* Target variable. */
  if (!parse_var_or_vec (c))
    goto fail;

  /* Target expression. */
  
  if (!lex_force_match ('=')
      || !parse_target_expression (c, func_tab))
    goto fail;

  add_transformation ((struct trns_header *) c);

  return CMD_SUCCESS;

fail:
  delete_trns (c);
  return CMD_FAILURE;
}

static int
if_num (struct trns_header * pt, struct ccase * c)
{
  struct compute_trns *t = (struct compute_trns *) pt;

  if (expr_evaluate (t->test, c, NULL) == 1.0)
    expr_evaluate (t->target, c, &c->data[t->fv]);
  return -1;
}

static int
if_str (struct trns_header * pt, struct ccase * c)
{
  struct compute_trns *t = (struct compute_trns *) pt;

  if (expr_evaluate (t->test, c, NULL) == 1.0)
    {
      union value v;

      expr_evaluate (t->target, c, &v);
      st_bare_pad_len_copy (c->data[t->fv].s, &v.c[1], t->width, v.c[0]);
    }
  return -1;
}

static int
if_num_vec (struct trns_header * pt, struct ccase * c)
{
  struct compute_trns *t = (struct compute_trns *) pt;

  if (expr_evaluate (t->test, c, NULL) == 1.0)
    {
      /* Index into the vector. */
      union value index;

      /* Rounded index value. */
      int rindx;

      expr_evaluate (t->vec_elem, c, &index);
      rindx = floor (index.f + EPSILON);
      if (index.f == SYSMIS || rindx < 1 || rindx > t->vec->nv)
	{
	  if (index.f == SYSMIS)
	    msg (SW, _("When executing COMPUTE: SYSMIS is not a valid value as "
		 "an index into vector %s."), t->vec->name);
	  else
	    msg (SW, _("When executing COMPUTE: %g is not a valid value as "
		 "an index into vector %s."), index.f, t->vec->name);
	  return -1;
	}
      expr_evaluate (t->target, c,
			   &c->data[t->vec->v[rindx]->fv]);
    }
  return -1;
}

static int
if_str_vec (struct trns_header * pt, struct ccase * c)
{
  struct compute_trns *t = (struct compute_trns *) pt;

  if (expr_evaluate (t->test, c, NULL) == 1.0)
    {
      /* Index into the vector. */
      union value index;

      /* Rounded index value. */
      int rindx;

      /* Temporary storage for result of target expression. */
      union value v2;

      /* Variable reference by indexed vector. */
      struct variable *vr;

      expr_evaluate (t->vec_elem, c, &index);
      rindx = floor (index.f + EPSILON);
      if (index.f == SYSMIS || rindx < 1 || rindx > t->vec->nv)
	{
	  if (index.f == SYSMIS)
	    msg (SW, _("When executing COMPUTE: SYSMIS is not a valid value as "
		 "an index into vector %s."), t->vec->name);
	  else
	    msg (SW, _("When executing COMPUTE: %g is not a valid value as "
		 "an index into vector %s."), index.f, t->vec->name);
	  return -1;
	}
      expr_evaluate (t->target, c, &v2);
      vr = t->vec->v[rindx - 1];
      st_bare_pad_len_copy (c->data[vr->fv].s, &v2.c[1], vr->width, v2.c[0]);
    }
  return -1;
}

/* Code common to COMPUTE and IF. */

/* Checks for type mismatches on transformation C.  Also checks for
   command terminator, sets the case-handling proc from the array
   passed. */
static int
parse_target_expression (struct compute_trns *c,
			 int (*proc_list[4]) (struct trns_header *, struct ccase *))
{
  int dest_type = c->v ? c->v->type : c->vec->v[0]->type;
  c->target = expr_parse (dest_type == ALPHA ? PXP_STRING : PXP_NUMERIC);
  if (!c->target)
    return 0;

  c->h.proc = proc_list[(dest_type == ALPHA) + 2 * (c->vec != NULL)];

  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return 0;
    }
  
  return 1;
}

/* Returns a new struct compute_trns after initializing its fields. */
static struct compute_trns *
new_trns (void)
{
  struct compute_trns *c = xmalloc (sizeof *c);
  c->h.proc = NULL;
  c->h.free = free_trns;
  c->v = NULL;
  c->created = 0;
  c->vec = NULL;
  c->fv = 0;
  c->width = 0;
  c->vec_elem = NULL;
  c->target = NULL;
  c->test = NULL;
  return c;
}

/* Deletes all the fields in C, the variable C->v if we created it,
   and C itself. */
static void
delete_trns (struct compute_trns * c)
{
  free_trns ((struct trns_header *) c);
  if (c->created)
    delete_variable (&default_dict, c->v);
  free (c);
}

/* Deletes all the fields in C. */
static void
free_trns (struct trns_header * pt)
{
  struct compute_trns *t = (struct compute_trns *) pt;

  expr_free (t->vec_elem);
  expr_free (t->target);
  expr_free (t->test);
}

/* Parses a variable name or a vector element into C.  If the
   variable does not exist, it is created.  Returns success. */
static int
parse_var_or_vec (struct compute_trns * c)
{
  if (!lex_force_id ())
    return 0;
  
  if (lex_look_ahead () == '(')
    {
      /* Vector element. */
      c->vec = find_vector (tokid);
      if (!c->vec)
	{
	  msg (SE, _("There is no vector named %s."), tokid);
	  return 0;
	}
      
      lex_get ();
      if (!lex_force_match ('('))
	return 0;
      c->vec_elem = expr_parse (PXP_NUMERIC);
      if (!c->vec_elem)
	return 0;
      if (!lex_force_match (')'))
	{
	  expr_free (c->vec_elem);
	  return 0;
	}
    }
  else
    {
      /* Variable name. */
      c->v = find_variable (tokid);
      if (!c->v)
	{
	  c->v = force_create_variable (&default_dict, tokid, NUMERIC, 0);
	  envector (c->v);
	  c->created = 1;
	}
      c->fv = c->v->fv;
      c->width = c->v->width;
      lex_get ();
    }
  return 1;
}

/* EVALUATE. */

#if GLOBAL_DEBUGGING
int
cmd_evaluate (void)
{
  struct expression *expr;

  lex_match_id ("EVALUATE");
  expr = expr_parse (PXP_DUMP);
  if (!expr)
    return CMD_FAILURE;

  expr_free (expr);
  if (token != '.')
    {
      msg (SE, _("Extra characters after expression."));
      return CMD_FAILURE;
    }
  
  return CMD_SUCCESS;
}
#endif
