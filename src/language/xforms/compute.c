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
#include <libpspp/message.h>
#include <stdlib.h>
#include <libpspp/alloc.h>
#include <data/case.h>
#include <language/command.h>
#include <data/dictionary.h>
#include <libpspp/message.h>
#include <language/expressions/public.h>
#include <language/lexer/lexer.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>
#include <data/variable.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

struct compute_trns;
struct lvalue;

/* Target of a COMPUTE or IF assignment, either a variable or a
   vector element. */
static struct lvalue *lvalue_parse (void);
static int lvalue_get_type (const struct lvalue *);
static bool lvalue_is_vector (const struct lvalue *);
static void lvalue_finalize (struct lvalue *,
                             struct compute_trns *);
static void lvalue_destroy (struct lvalue *);

/* COMPUTE and IF transformation. */
struct compute_trns
  {
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

static struct expression *parse_rvalue (const struct lvalue *);
static struct compute_trns *compute_trns_create (void);
static trns_proc_func *get_proc_func (const struct lvalue *);
static trns_free_func compute_trns_free;

/* COMPUTE. */

int
cmd_compute (void)
{
  struct lvalue *lvalue = NULL;
  struct compute_trns *compute = NULL;

  compute = compute_trns_create ();

  lvalue = lvalue_parse ();
  if (lvalue == NULL)
    goto fail;

  if (!lex_force_match ('='))
    goto fail;
  compute->rvalue = parse_rvalue (lvalue);
  if (compute->rvalue == NULL)
    goto fail;

  add_transformation (get_proc_func (lvalue), compute_trns_free, compute);

  lvalue_finalize (lvalue, compute);

  return lex_end_of_command ();

 fail:
  lvalue_destroy (lvalue);
  compute_trns_free (compute);
  return CMD_CASCADING_FAILURE;
}

/* Transformation functions. */

/* Handle COMPUTE or IF with numeric target variable. */
static int
compute_num (void *compute_, struct ccase *c, int case_num)
{
  struct compute_trns *compute = compute_;

  if (compute->test == NULL
      || expr_evaluate_num (compute->test, c, case_num) == 1.0) 
    case_data_rw (c, compute->fv)->f = expr_evaluate_num (compute->rvalue, c,
                                                          case_num); 
  
  return TRNS_CONTINUE;
}

/* Handle COMPUTE or IF with numeric vector element target
   variable. */
static int
compute_num_vec (void *compute_, struct ccase *c, int case_num)
{
  struct compute_trns *compute = compute_;

  if (compute->test == NULL
      || expr_evaluate_num (compute->test, c, case_num) == 1.0) 
    {
      double index;     /* Index into the vector. */
      int rindx;        /* Rounded index value. */

      index = expr_evaluate_num (compute->element, c, case_num);
      rindx = floor (index + EPSILON);
      if (index == SYSMIS || rindx < 1 || rindx > compute->vector->cnt)
        {
          if (index == SYSMIS)
            msg (SW, _("When executing COMPUTE: SYSMIS is not a valid value as "
                       "an index into vector %s."), compute->vector->name);
          else
            msg (SW, _("When executing COMPUTE: %g is not a valid value as "
                       "an index into vector %s."),
                 index, compute->vector->name);
          return TRNS_CONTINUE;
        }
      case_data_rw (c, compute->vector->var[rindx - 1]->fv)->f
        = expr_evaluate_num (compute->rvalue, c, case_num);
    }
  
  return TRNS_CONTINUE;
}

/* Handle COMPUTE or IF with string target variable. */
static int
compute_str (void *compute_, struct ccase *c, int case_num)
{
  struct compute_trns *compute = compute_;

  if (compute->test == NULL
      || expr_evaluate_num (compute->test, c, case_num) == 1.0) 
    expr_evaluate_str (compute->rvalue, c, case_num,
                       case_data_rw (c, compute->fv)->s, compute->width);
  
  return TRNS_CONTINUE;
}

/* Handle COMPUTE or IF with string vector element target
   variable. */
static int
compute_str_vec (void *compute_, struct ccase *c, int case_num)
{
  struct compute_trns *compute = compute_;

  if (compute->test == NULL
      || expr_evaluate_num (compute->test, c, case_num) == 1.0) 
    {
      double index;             /* Index into the vector. */
      int rindx;                /* Rounded index value. */
      struct variable *vr;      /* Variable reference by indexed vector. */

      index = expr_evaluate_num (compute->element, c, case_num);
      rindx = floor (index + EPSILON);
      if (index == SYSMIS) 
        {
          msg (SW, _("When executing COMPUTE: SYSMIS is not a valid "
                     "value as an index into vector %s."),
               compute->vector->name);
          return TRNS_CONTINUE; 
        }
      else if (rindx < 1 || rindx > compute->vector->cnt)
        {
          msg (SW, _("When executing COMPUTE: %g is not a valid value as "
                     "an index into vector %s."),
               index, compute->vector->name);
          return TRNS_CONTINUE;
        }

      vr = compute->vector->var[rindx - 1];
      expr_evaluate_str (compute->rvalue, c, case_num,
                         case_data_rw (c, vr->fv)->s, vr->width);
    }
  
  return TRNS_CONTINUE;
}

/* IF. */

int
cmd_if (void)
{
  struct compute_trns *compute = NULL;
  struct lvalue *lvalue = NULL;

  compute = compute_trns_create ();

  /* Test expression. */
  compute->test = expr_parse (default_dict, EXPR_BOOLEAN);
  if (compute->test == NULL)
    goto fail;

  /* Lvalue variable. */
  lvalue = lvalue_parse ();
  if (lvalue == NULL)
    goto fail;

  /* Rvalue expression. */
  if (!lex_force_match ('='))
    goto fail;
  compute->rvalue = parse_rvalue (lvalue);
  if (compute->rvalue == NULL)
    goto fail;

  add_transformation (get_proc_func (lvalue), compute_trns_free, compute);

  lvalue_finalize (lvalue, compute);

  return lex_end_of_command ();

 fail:
  lvalue_destroy (lvalue);
  compute_trns_free (compute);
  return CMD_CASCADING_FAILURE;
}

/* Code common to COMPUTE and IF. */

static trns_proc_func *
get_proc_func (const struct lvalue *lvalue) 
{
  bool is_numeric = lvalue_get_type (lvalue) == NUMERIC;
  bool is_vector = lvalue_is_vector (lvalue);

  return (is_numeric
          ? (is_vector ? compute_num_vec : compute_num)
          : (is_vector ? compute_str_vec : compute_str));
}

/* Parses and returns an rvalue expression of the same type as
   LVALUE, or a null pointer on failure. */
static struct expression *
parse_rvalue (const struct lvalue *lvalue)
{
  bool is_numeric = lvalue_get_type (lvalue) == NUMERIC;

  return expr_parse (default_dict, is_numeric ? EXPR_NUMBER : EXPR_STRING);
}

/* Returns a new struct compute_trns after initializing its fields. */
static struct compute_trns *
compute_trns_create (void)
{
  struct compute_trns *compute = xmalloc (sizeof *compute);
  compute->test = NULL;
  compute->variable = NULL;
  compute->vector = NULL;
  compute->element = NULL;
  compute->rvalue = NULL;
  return compute;
}

/* Deletes all the fields in COMPUTE. */
static bool
compute_trns_free (void *compute_)
{
  struct compute_trns *compute = compute_;

  if (compute != NULL) 
    {
      expr_free (compute->test);
      expr_free (compute->element);
      expr_free (compute->rvalue);
      free (compute);
    }
  return true;
}

/* COMPUTE or IF target variable or vector element. */
struct lvalue
  {
    char var_name[LONG_NAME_LEN + 1];   /* Destination variable name, or "". */
    const struct vector *vector; /* Destination vector, if any, or NULL. */
    struct expression *element;  /* Destination vector element, or NULL. */
  };

/* Parses the target variable or vector element into a new
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
      lvalue->element = expr_parse (default_dict, EXPR_NUMBER);
      if (lvalue->element == NULL)
        goto lossage;
      if (!lex_force_match (')'))
        goto lossage;
    }
  else
    {
      /* Variable name. */
      str_copy_trunc (lvalue->var_name, sizeof lvalue->var_name, tokid);
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
      struct variable *var = dict_lookup_var (default_dict, lvalue->var_name);
      if (var == NULL)
        return NUMERIC;
      else
        return var->type;
    }
  else 
    return lvalue->vector->var[0]->type;
}

/* Returns nonzero if LVALUE has a vector as its target. */
static bool
lvalue_is_vector (const struct lvalue *lvalue) 
{
  return lvalue->vector != NULL;
}

/* Finalizes making LVALUE the target of COMPUTE, by creating the
   target variable if necessary and setting fields in COMPUTE. */
static void
lvalue_finalize (struct lvalue *lvalue, struct compute_trns *compute) 
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
        compute->variable->leave = false;
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
  if (lvalue == NULL) 
     return;

  expr_free (lvalue->element);
  free (lvalue);
}
