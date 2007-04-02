/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000 Free Software Foundation, Inc.

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
#include <language/lexer/range-parser.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/compiler.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/pool.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Value or range? */
enum value_type
  {
    CNT_SINGLE,			/* Single value. */
    CNT_RANGE			/* a <= x <= b. */
  };

/* Numeric count criteria. */
struct num_value
  {
    enum value_type type;       /* How to interpret a, b. */
    double a, b;                /* Values to count. */
  };

struct criteria
  {
    struct criteria *next;

    /* Variables to count. */
    const struct variable **vars;
    size_t var_cnt;

    /* Count special values? */
    bool count_system_missing;  /* Count system missing? */
    bool count_user_missing;    /* Count user missing? */

    /* Criterion values. */    
    size_t value_cnt;
    union
      {
	struct num_value *num;
	char **str;
      }
    values;
  };

struct dst_var
  {
    struct dst_var *next;
    struct variable *var;       /* Destination variable. */
    char *name;                 /* Name of dest var. */
    struct criteria *crit;      /* The criteria specifications. */
  };

struct count_trns
  {
    struct dst_var *dst_vars;
    struct pool *pool;
  };

static trns_proc_func count_trns_proc;
static trns_free_func count_trns_free;

static bool parse_numeric_criteria (struct lexer *, struct pool *, struct criteria *);
static bool parse_string_criteria (struct lexer *, struct pool *, struct criteria *);

int
cmd_count (struct lexer *lexer, struct dataset *ds)
{
  struct dst_var *dv;           /* Destination var being parsed. */
  struct count_trns *trns;      /* Transformation. */

  /* Parses each slash-delimited specification. */
  trns = pool_create_container (struct count_trns, pool);
  trns->dst_vars = dv = pool_alloc (trns->pool, sizeof *dv);
  for (;;)
    {
      struct criteria *crit;

      /* Initialize this struct dst_var to ensure proper cleanup. */
      dv->next = NULL;
      dv->var = NULL;
      dv->crit = NULL;

      /* Get destination variable, or at least its name. */
      if (!lex_force_id (lexer))
	goto fail;
      dv->var = dict_lookup_var (dataset_dict (ds), lex_tokid (lexer));
      if (dv->var != NULL)
        {
          if (var_is_alpha (dv->var))
            {
              msg (SE, _("Destination cannot be a string variable."));
              goto fail;
            }
        }
      else
        dv->name = pool_strdup (trns->pool, lex_tokid (lexer));

      lex_get (lexer);
      if (!lex_force_match (lexer, '='))
	goto fail;

      crit = dv->crit = pool_alloc (trns->pool, sizeof *crit);
      for (;;)
	{
          bool ok;

	  crit->next = NULL;
	  crit->vars = NULL;
	  if (!parse_variables_const (lexer, dataset_dict (ds), &crit->vars, 
				      &crit->var_cnt,
                                PV_DUPLICATE | PV_SAME_TYPE))
	    goto fail;
          pool_register (trns->pool, free, crit->vars);

	  if (!lex_force_match (lexer, '('))
	    goto fail;

          crit->value_cnt = 0;
          if (var_is_numeric (crit->vars[0]))
            ok = parse_numeric_criteria (lexer, trns->pool, crit);
          else
            ok = parse_string_criteria (lexer, trns->pool, crit);
	  if (!ok)
	    goto fail;

	  if (lex_token (lexer) == '/' || lex_token (lexer) == '.')
	    break;

	  crit = crit->next = pool_alloc (trns->pool, sizeof *crit);
	}

      if (lex_token (lexer) == '.')
	break;

      if (!lex_force_match (lexer, '/'))
	goto fail;
      dv = dv->next = pool_alloc (trns->pool, sizeof *dv);
    }

  /* Create all the nonexistent destination variables. */
  for (dv = trns->dst_vars; dv; dv = dv->next)
    if (dv->var == NULL)
      {
	/* It's valid, though motivationally questionable, to count to
	   the same dest var more than once. */
	dv->var = dict_lookup_var (dataset_dict (ds), dv->name);

	if (dv->var == NULL) 
          dv->var = dict_create_var_assert (dataset_dict (ds), dv->name, 0);
      }

  add_transformation (ds, count_trns_proc, count_trns_free, trns);
  return CMD_SUCCESS;

fail:
  count_trns_free (trns);
  return CMD_FAILURE;
}

/* Parses a set of numeric criterion values.  Returns success. */
static bool
parse_numeric_criteria (struct lexer *lexer, struct pool *pool, struct criteria *crit)
{
  size_t allocated = 0;

  crit->values.num = NULL;
  crit->count_system_missing = false;
  crit->count_user_missing = false;
  for (;;)
    {
      double low, high;
      
      if (lex_match_id (lexer, "SYSMIS"))
        crit->count_system_missing = true;
      else if (lex_match_id (lexer, "MISSING"))
	crit->count_user_missing = true;
      else if (parse_num_range (lexer, &low, &high, NULL)) 
        {
          struct num_value *cur;

          if (crit->value_cnt >= allocated)
            crit->values.num = pool_2nrealloc (pool, crit->values.num,
                                               &allocated,
                                               sizeof *crit->values.num);
          cur = &crit->values.num[crit->value_cnt++];
          cur->type = low == high ? CNT_SINGLE : CNT_RANGE;
          cur->a = low;
          cur->b = high;
        }
      else
        return false;

      lex_match (lexer, ',');
      if (lex_match (lexer, ')'))
	break;
    }
  return true;
}

/* Parses a set of string criteria values.  Returns success. */
static bool
parse_string_criteria (struct lexer *lexer, struct pool *pool, struct criteria *crit)
{
  int len = 0;
  size_t allocated = 0;
  size_t i;

  for (i = 0; i < crit->var_cnt; i++)
    if (var_get_width (crit->vars[i]) > len)
      len = var_get_width (crit->vars[i]);

  crit->values.str = NULL;
  for (;;)
    {
      char **cur;
      if (crit->value_cnt >= allocated)
        crit->values.str = pool_2nrealloc (pool, crit->values.str,
                                           &allocated,
                                           sizeof *crit->values.str);

      if (!lex_force_string (lexer))
	return false;
      cur = &crit->values.str[crit->value_cnt++];
      *cur = pool_alloc (pool, len + 1);
      str_copy_rpad (*cur, len + 1, ds_cstr (lex_tokstr (lexer)));
      lex_get (lexer);

      lex_match (lexer, ',');
      if (lex_match (lexer, ')'))
	break;
    }

  return true;
}

/* Transformation. */

/* Counts the number of values in case C matching CRIT. */
static inline int
count_numeric (struct criteria *crit, struct ccase *c)
{
  int counter = 0;
  size_t i;

  for (i = 0; i < crit->var_cnt; i++)
    {
      double x = case_num (c, crit->vars[i]);
      if (var_is_num_missing (crit->vars[i], x, MV_ANY))
        {
          if (x == SYSMIS
              ? crit->count_system_missing
              : crit->count_user_missing)
            counter++; 
        }
      else 
        {
          struct num_value *v;
          
          for (v = crit->values.num; v < crit->values.num + crit->value_cnt;
               v++) 
            if (v->type == CNT_SINGLE ? x == v->a : x >= v->a && x <= v->b) 
              {
                counter++;
                break;
              } 
        }
    }
  
  return counter;
}

/* Counts the number of values in case C matching CRIT. */
static inline int
count_string (struct criteria *crit, struct ccase *c)
{
  int counter = 0;
  size_t i;

  for (i = 0; i < crit->var_cnt; i++)
    {
      char **v;
      for (v = crit->values.str; v < crit->values.str + crit->value_cnt; v++)
        if (!memcmp (case_str (c, crit->vars[i]), *v,
                     var_get_width (crit->vars[i])))
          {
	    counter++;
            break;
          }
    }

  return counter;
}

/* Performs the COUNT transformation T on case C. */
static int
count_trns_proc (void *trns_, struct ccase *c,
                 casenumber case_num UNUSED)
{
  struct count_trns *trns = trns_;
  struct dst_var *dv;

  for (dv = trns->dst_vars; dv; dv = dv->next)
    {
      struct criteria *crit;
      int counter;

      counter = 0;
      for (crit = dv->crit; crit; crit = crit->next)
	if (var_is_numeric (crit->vars[0]))
	  counter += count_numeric (crit, c);
	else
	  counter += count_string (crit, c);
      case_data_rw (c, dv->var)->f = counter;
    }
  return TRNS_CONTINUE;
}

/* Destroys all dynamic data structures associated with TRNS. */
static bool
count_trns_free (void *trns_)
{
  struct count_trns *trns = (struct count_trns *) trns_;
  pool_destroy (trns->pool);
  return true;
}
