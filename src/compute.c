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
#include "case.h"
#include "command.h"
#include "dictionary.h"
#include "error.h"
#include "expr.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "var.h"

struct compute_trns;
struct lvalue;

/* Target of a COMPUTE or IF assignment, either a variable or a
   vector element. */
static struct lvalue *lvalue_parse (void);
static int lvalue_get_type (const struct lvalue *);
static int lvalue_is_vector (const struct lvalue *);
static void lvalue_finalize (struct lvalue *,
                             struct compute_trns *);
static void lvalue_destroy (struct lvalue *);

/* COMPUTE and IF transformation. */
struct compute_trns
  {
    struct trns_header h;

    /* Test expression (IF only). */
    struct expression *test;	 /* Test expression. */

    /* Variable lvalue, if variable != NULL. */
    struct variable *variable;   /* Destination variable, if any. */
    int fv;			 /* `value' index of destination variable. */
    int width;			 /* Lvalue string width; 0=numeric. */

    /* Vector lvalue, if vector != NULL. */
    const struct vector *vector; /* Destination vector, if any. */
    struct expression *element;  /* Destination vector element expr. */

    /* Rvalue. */
    struct expression *rvalue;	 /* Rvalue expression. */
  };

static int parse_rvalue_expression (struct compute_trns *,
                                    const struct lvalue *);
static struct compute_trns *compute_trns_create (void);
static void compute_trns_free (struct trns_header *);

/* COMPUTE. */

int
cmd_compute (void)
{
  struct lvalue *lvalue = NULL;
  struct compute_trns *compute = NULL;

  lvalue = lvalue_parse ();
  if (lvalue == NULL)
    goto fail;

  compute = compute_trns_create ();

  if (!lex_force_match ('=') || !parse_rvalue_expression (compute, lvalue))
    goto fail;

  lvalue_finalize (lvalue, compute);

  add_transformation (&compute->h);

  return CMD_SUCCESS;

 fail:
  lvalue_destroy (lvalue);
  if (compute != NULL) 
    {
      compute_trns_free (&compute->h);
      free (compute); 
    }
  return CMD_FAILURE;
}

/* Transformation functions. */

/* Handle COMPUTE or IF with numeric target variable. */
static int
compute_num (struct trns_header *compute_, struct ccase *c,
             int case_num)
{
  struct compute_trns *compute = (struct compute_trns *) compute_;

  if (compute->test == NULL
      || expr_evaluate (compute->test, c, case_num, NULL) == 1.0) 
    {
      expr_evaluate (compute->rvalue, c, case_num,
                     case_data_rw (c, compute->fv)); 
    }
  
  return -1;
}

/* Handle COMPUTE or IF with numeric vector element target
   variable. */
static int
compute_num_vec (struct trns_header *compute_, struct ccase *c,
                 int case_num)
{
  struct compute_trns *compute = (struct compute_trns *) compute_;

  if (compute->test == NULL
      || expr_evaluate (compute->test, c, case_num, NULL) == 1.0) 
    {
      /* Index into the vector. */
      union value index;

      /* Rounded index value. */
      int rindx;

      expr_evaluate (compute->element, c, case_num, &index);
      rindx = floor (index.f + EPSILON);
      if (index.f == SYSMIS || rindx < 1 || rindx > compute->vector->cnt)
        {
          if (index.f == SYSMIS)
            msg (SW, _("When executing COMPUTE: SYSMIS is not a valid value as "
                       "an index into vector %s."), compute->vector->name);
          else
            msg (SW, _("When executing COMPUTE: %g is not a valid value as "
                       "an index into vector %s."),
                 index.f, compute->vector->name);
          return -1;
        }
      expr_evaluate (compute->rvalue, c, case_num,
                     case_data_rw (c, compute->vector->var[rindx - 1]->fv));
    }
  
  return -1;
}

/* Handle COMPUTE or IF with string target variable. */
static int
compute_str (struct trns_header *compute_, struct ccase *c,
             int case_num)
{
  struct compute_trns *compute = (struct compute_trns *) compute_;

  if (compute->test == NULL
      || expr_evaluate (compute->test, c, case_num, NULL) == 1.0) 
    {
      /* Temporary storage for string expression return value. */
      union value v;

      expr_evaluate (compute->rvalue, c, case_num, &v);
      st_bare_pad_len_copy (case_data_rw (c, compute->fv)->s,
                            &v.c[1], compute->width, v.c[0]); 
    }
  
  return -1;
}

/* Handle COMPUTE or IF with string vector element target
   variable. */
static int
compute_str_vec (struct trns_header *compute_, struct ccase *c,
                 int case_num)
{
  struct compute_trns *compute = (struct compute_trns *) compute_;

  if (compute->test == NULL
      || expr_evaluate (compute->test, c, case_num, NULL) == 1.0) 
    {
      /* Temporary storage for string expression return value. */
      union value v;

      /* Index into the vector. */
      union value index;

      /* Rounded index value. */
      int rindx;

      /* Variable reference by indexed vector. */
      struct variable *vr;

      expr_evaluate (compute->element, c, case_num, &index);
      rindx = floor (index.f + EPSILON);
      if (index.f == SYSMIS || rindx < 1 || rindx > compute->vector->cnt)
        {
          if (index.f == SYSMIS)
            msg (SW, _("When executing COMPUTE: SYSMIS is not a valid "
                       "value as an index into vector %s."),
                 compute->vector->name);
          else
            msg (SW, _("When executing COMPUTE: %g is not a valid value as "
                       "an index into vector %s."),
                 index.f, compute->vector->name);
          return -1;
        }

      expr_evaluate (compute->rvalue, c, case_num, &v);
      vr = compute->vector->var[rindx - 1];
      st_bare_pad_len_copy (case_data_rw (c, vr->fv)->s,
                            &v.c[1], vr->width, v.c[0]); 
    }
  
  return -1;
}

/* IF. */

int
cmd_if (void)
{
  struct compute_trns *compute = NULL;
  struct lvalue *lvalue = NULL;

  compute = compute_trns_create ();

  /* Test expression. */
  compute->test = expr_parse (EXPR_BOOLEAN);
  if (compute->test == NULL)
    goto fail;

  /* Lvalue variable. */
  lvalue = lvalue_parse ();
  if (lvalue == NULL)
    goto fail;

  /* Rvalue expression. */
  if (!lex_force_match ('=') || !parse_rvalue_expression (compute, lvalue))
    goto fail;

  lvalue_finalize (lvalue, compute);

  add_transformation (&compute->h);

  return CMD_SUCCESS;

 fail:
  lvalue_destroy (lvalue);
  if (compute != NULL) 
    {
      compute_trns_free (&compute->h);
      free (compute); 
    }
  return CMD_FAILURE;
}

/* Code common to COMPUTE and IF. */

/* Checks for type mismatches on transformation C.  Also checks for
   command terminator, sets the case-handling proc from the array
   passed. */
static int
parse_rvalue_expression (struct compute_trns *compute,
                         const struct lvalue *lvalue)
{
  int type = lvalue_get_type (lvalue);
  int vector = lvalue_is_vector (lvalue);

  assert (type == NUMERIC || type == ALPHA);

  compute->rvalue = expr_parse (type == ALPHA ? EXPR_STRING : EXPR_NUMERIC);
  if (compute->rvalue == NULL)
    return 0;

  if (type == NUMERIC)
    compute->h.proc = vector ? compute_num_vec : compute_num;
  else
    compute->h.proc = vector ? compute_str_vec : compute_str;

  if (token != '.')
    {
      lex_error (_("expecting end of command"));
      return 0;
    }
  
  return 1;
}

/* Returns a new struct compute_trns after initializing its fields. */
static struct compute_trns *
compute_trns_create (void)
{
  struct compute_trns *compute = xmalloc (sizeof *compute);
  compute->h.proc = NULL;
  compute->h.free = compute_trns_free;
  compute->test = NULL;
  compute->variable = NULL;
  compute->vector = NULL;
  compute->element = NULL;
  compute->rvalue = NULL;
  return compute;
}

/* Deletes all the fields in COMPUTE. */
static void
compute_trns_free (struct trns_header *compute_)
{
  struct compute_trns *compute = (struct compute_trns *) compute_;

  expr_free (compute->test);
  expr_free (compute->element);
  expr_free (compute->rvalue);
}

/* COMPUTE or IF target variable or vector element. */
struct lvalue
  {
    char var_name[9];            /* Destination variable name, or "". */
    const struct vector *vector; /* Destination vector, if any, or NULL. */
    struct expression *element;  /* Destination vector element, or NULL. */
  };

/* Parses the target variable or vector elector into a new
   `struct lvalue', which is returned. */
static struct lvalue *
lvalue_parse (void) 
{
  struct lvalue *lvalue;

  lvalue = xmalloc (sizeof *lvalue);
  lvalue->var_name[0] = '\0';
  lvalue->vector = NULL;
  lvalue->element = NULL;

  if (!lex_force_id ())
    goto lossage;
  
  if (lex_look_ahead () == '(')
    {
      /* Vector. */
      lvalue->vector = dict_lookup_vector (default_dict, tokid);
      if (lvalue->vector == NULL)
	{
	  msg (SE, _("There is no vector named %s."), tokid);
          goto lossage;
	}

      /* Vector element. */
      lex_get ();
      if (!lex_force_match ('('))
	goto lossage;
      lvalue->element = expr_parse (EXPR_NUMERIC);
      if (lvalue->element == NULL)
        goto lossage;
      if (!lex_force_match (')'))
        goto lossage;
    }
  else
    {
      /* Variable name. */
      strncpy (lvalue->var_name, tokid, 8);
      lvalue->var_name[8] = '\0';
      lex_get ();
    }
  return lvalue;

 lossage:
  lvalue_destroy (lvalue);
  return NULL;
}

/* Returns the type (NUMERIC or ALPHA) of the target variable or
   vector in LVALUE. */
static int
lvalue_get_type (const struct lvalue *lvalue) 
{
  if (lvalue->vector == NULL) 
    {
      struct variable *var
        = dict_lookup_var (default_dict, lvalue->var_name);
      if (var == NULL)
        return NUMERIC;
      else
        return var->type;
    }
  else 
    return lvalue->vector->var[0]->type;
}

/* Returns nonzero if LVALUE has a vector as its target. */
static int
lvalue_is_vector (const struct lvalue *lvalue) 
{
  return lvalue->vector != NULL;
}

/* Finalizes making LVALUE the target of COMPUTE, by creating the
   target variable if necessary and setting fields in COMPUTE. */
static void
lvalue_finalize (struct lvalue *lvalue,
                 struct compute_trns *compute) 
{
  if (lvalue->vector == NULL)
    {
      compute->variable = dict_lookup_var (default_dict, lvalue->var_name);
      if (compute->variable == NULL)
	  compute->variable = dict_create_var_assert (default_dict,
						      lvalue->var_name, 0);

      compute->fv = compute->variable->fv;
      compute->width = compute->variable->width;

      /* Goofy behavior, but compatible: Turn off LEAVE. */
      if (dict_class_from_id (compute->variable->name) != DC_SCRATCH)
        compute->variable->reinit = 1;
    }
  else 
    {
      compute->vector = lvalue->vector;
      compute->element = lvalue->element;
      lvalue->element = NULL;
    }

  lvalue_destroy (lvalue);
}

/* Destroys LVALUE. */
static void 
lvalue_destroy (struct lvalue *lvalue) 
{
  if ( ! lvalue ) 
     return ;

  expr_free (lvalue->element);
  free (lvalue);
}
