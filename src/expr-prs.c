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
#include "expr.h"
#include "exprP.h"
#include <assert.h>
#include <ctype.h>
#include <float.h>
#include <stdlib.h>
#include "algorithm.h"
#include "alloc.h"
#include "error.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "var.h"
#include "vfm.h"
#include "pool.h"

/* Declarations. */

/* Lowest precedence. */
static int parse_or (union any_node **n);
static int parse_and (union any_node **n);
static int parse_not (union any_node **n);
static int parse_rel (union any_node **n);
static int parse_add (union any_node **n);
static int parse_mul (union any_node **n);
static int parse_neg (union any_node **n);
static int parse_exp (union any_node **n);
static int parse_primary (union any_node **n);
static int parse_function (union any_node **n);
/* Highest precedence. */

/* Utility functions. */
static const char *expr_type_name (int type);
static const char *type_name (int type);
static void make_bool (union any_node **n);
static union any_node *allocate_nonterminal (int op, union any_node *n);
static union any_node *append_nonterminal_arg (union any_node *,
					       union any_node *);
static int type_check (union any_node **n, int type, int flags);

static algo_compare_func compare_functions;
static void init_func_tab (void);

#if DEBUGGING
static void debug_print_tree (union any_node *, int);
#endif

#if GLOBAL_DEBUGGING
static void debug_print_postfix (struct expression *);
#endif

/* Public functions. */

void
expr_free (struct expression *e)
{
  if (e == NULL)
    return;

  free (e->op);
  free (e->var);
  free (e->num);
  free (e->str);
  free (e->stack);
  pool_destroy (e->pool);
  free (e);
}

struct expression *
expr_parse (int flags)
{
  struct expression *e;
  union any_node *n;
  int type;

  /* Make sure the table of functions is initialized. */
  init_func_tab ();

  /* Parse the expression. */
  type = parse_or (&n);
  if (type == EX_ERROR)
    return NULL;

  /* Enforce type rules. */
  if (!type_check (&n, type, flags))
    {
      free_node (n);
      return NULL;
    }

  /* Optimize the expression as best we can. */
  n = (union any_node *) optimize_expression ((struct nonterm_node *) n);

  /* Dump the tree-based expression to a postfix representation for
     best evaluation speed, and destroy the tree. */
  e = xmalloc (sizeof *e);
  e->type = type;
  dump_expression (n, e);
  free_node (n);

  /* If we're debugging or the user requested it, print the postfix
     representation. */
#if GLOBAL_DEBUGGING
#if !DEBUGGING
  if (flags & PXP_DUMP)
#endif
    debug_print_postfix (e);
#endif

  return e;
}

static int
type_check (union any_node **n, int type, int flags)
{
  /* Enforce PXP_BOOLEAN flag. */
  if (flags & PXP_BOOLEAN)
    {
      if (type == EX_STRING)
	{
	  msg (SE, _("A string expression was supplied in a place "
		     "where a Boolean expression was expected."));
	  return 0;
	}
      else if (type == EX_NUMERIC)
	*n = allocate_nonterminal (OP_NUM_TO_BOOL, *n);
    }
  
  /* Enforce PXP_NUMERIC flag. */
  if ((flags & PXP_NUMERIC) && (type != EX_NUMERIC))
    {
      msg (SE, _("A numeric expression was expected in a place "
		 "where one was not supplied."));
      return 0;
    }

  /* Enforce PXP_STRING flag. */
  if ((flags & PXP_STRING) && (type != EX_STRING))
    {
      msg (SE, _("A string expression was expected in a place "
		 "where one was not supplied."));
      return 0;
    }

  return 1;
}

/* Recursive-descent expression parser. */

/* Parses the OR level. */
static int
parse_or (union any_node **n)
{
  char typ[] = N_("The OR operator cannot take string operands.");
  union any_node *c;
  int type;

  type = parse_and (n);
  if (type == EX_ERROR || token != T_OR)
    return type;
  if (type == EX_STRING)
    {
      free_node (*n);
      msg (SE, gettext (typ));
      return 0;
    }
  else if (type == EX_NUMERIC)
    make_bool (n);

  c = allocate_nonterminal (OP_OR, *n);
  for (;;)
    {
      lex_get ();
      type = parse_and (n);
      if (type == EX_ERROR)
	goto fail;
      else if (type == EX_STRING)
	{
	  msg (SE, gettext (typ));
	  goto fail;
	}
      else if (type == EX_NUMERIC)
	make_bool (n);
      c = append_nonterminal_arg (c, *n);

      if (token != T_OR)
	break;
    }
  *n = c;
  return EX_BOOLEAN;

fail:
  free_node (c);
  return EX_ERROR;
}

/* Parses the AND level. */
static int
parse_and (union any_node ** n)
{
  static const char typ[]
    = N_("The AND operator cannot take string operands.");
  union any_node *c;
  int type = parse_not (n);

  if (type == EX_ERROR)
    return EX_ERROR;
  if (token != T_AND)
    return type;
  if (type == EX_STRING)
    {
      free_node (*n);
      msg (SE, gettext (typ));
      return 0;
    }
  else if (type == EX_NUMERIC)
    make_bool (n);

  c = allocate_nonterminal (OP_AND, *n);
  for (;;)
    {
      lex_get ();
      type = parse_not (n);
      if (type == EX_ERROR)
	goto fail;
      else if (type == EX_STRING)
	{
	  msg (SE, gettext (typ));
	  goto fail;
	}
      else if (type == EX_NUMERIC)
	make_bool (n);
      c = append_nonterminal_arg (c, *n);

      if (token != T_AND)
	break;
    }
  *n = c;
  return EX_BOOLEAN;

fail:
  free_node (c);
  return EX_ERROR;
}

/* Parses the NOT level. */
static int
parse_not (union any_node ** n)
{
  static const char typ[]
    = N_("The NOT operator cannot take a string operand.");
  int not = 0;
  int type;

  while (lex_match (T_NOT))
    not ^= 1;
  type = parse_rel (n);
  if (!not || type == EX_ERROR)
    return type;

  if (type == EX_STRING)
    {
      free_node (*n);
      msg (SE, gettext (typ));
      return 0;
    }
  else if (type == EX_NUMERIC)
    make_bool (n);

  *n = allocate_nonterminal (OP_NOT, *n);
  return EX_BOOLEAN;
}

static int
parse_rel (union any_node ** n)
{
  static const char typ[]
    = N_("Strings cannot be compared with numeric or Boolean "
	 "values with the relational operators "
	 "= >= > <= < <>.");
  union any_node *c;
  int type = parse_add (n);

  if (type == EX_ERROR)
    return EX_ERROR;
  if (token == '=')
    token = T_EQ;
  if (token < T_EQ || token > T_NE)
    return type;

  for (;;)
    {
      int t;

      c = allocate_nonterminal (token - T_EQ
				+ (type == EX_NUMERIC ? OP_EQ : OP_STRING_EQ),
				*n);
      lex_get ();

      t = parse_add (n);
      if (t == EX_ERROR)
	goto fail;
      if (t == EX_BOOLEAN && type == EX_NUMERIC)
	make_bool (&c->nonterm.arg[0]);
      else if (t == EX_NUMERIC && type == EX_BOOLEAN)
	make_bool (n);
      else if (t != type)
	{
	  msg (SE, gettext (typ));
	  goto fail;
	}

      c = append_nonterminal_arg (c, *n);
      *n = c;

      if (token == '=')
	token = T_EQ;
      if (token < T_EQ || token > T_NE)
	break;

      type = EX_BOOLEAN;
    }
  return EX_BOOLEAN;

fail:
  free_node (c);
  return EX_ERROR;
}

/* Parses the addition and subtraction level. */
static int
parse_add (union any_node **n)
{
  static const char typ[]
    = N_("The `+' and `-' operators may only be used with "
	 "numeric operands.");
  union any_node *c;
  int type;
  int op;

  type = parse_mul (n);
  lex_negative_to_dash ();
  if (type == EX_ERROR || (token != '+' && token != '-'))
    return type;
  if (type != EX_NUMERIC)
    {
      free_node (*n);
      msg (SE, gettext (typ));
      return 0;
    }

  c = allocate_nonterminal (OP_PLUS, *n);
  for (;;)
    {
      op = token;
      lex_get ();

      type = parse_mul (n);
      if (type == EX_ERROR)
	goto fail;
      else if (type != EX_NUMERIC)
	{
	  msg (SE, gettext (typ));
	  goto fail;
	}
      if (op == '-')
	*n = allocate_nonterminal (OP_NEG, *n);
      c = append_nonterminal_arg (c, *n);

      lex_negative_to_dash ();
      if (token != '+' && token != '-')
	break;
    }
  *n = c;
  return EX_NUMERIC;

fail:
  free_node (c);
  return EX_ERROR;
}

/* Parses the multiplication and division level. */
static int
parse_mul (union any_node ** n)
{
  static const char typ[]
    = N_("The `*' and `/' operators may only be used with "
	 "numeric operands.");

  union any_node *c;
  int type;
  int op;

  type = parse_neg (n);
  if (type == EX_ERROR || (token != '*' && token != '/'))
    return type;
  if (type != EX_NUMERIC)
    {
      free_node (*n);
      msg (SE, gettext (typ));
      return 0;
    }

  c = allocate_nonterminal (OP_MUL, *n);
  for (;;)
    {
      op = token;
      lex_get ();

      type = parse_neg (n);
      if (type == EX_ERROR)
	goto fail;
      else if (type != EX_NUMERIC)
	{
	  msg (SE, gettext (typ));
	  goto fail;
	}
      if (op == '/')
	*n = allocate_nonterminal (OP_INV, *n);
      c = append_nonterminal_arg (c, *n);

      if (token != '*' && token != '/')
	break;
    }
  *n = c;
  return EX_NUMERIC;

fail:
  free_node (c);
  return EX_ERROR;
}

/* Parses the unary minus level. */
static int
parse_neg (union any_node **n)
{
  static const char typ[]
    = N_("The unary minus (-) operator can only take a numeric operand.");

  int neg = 0;
  int type;

  for (;;)
    {
      lex_negative_to_dash ();
      if (!lex_match ('-'))
	break;
      neg ^= 1;
    }
  type = parse_exp (n);
  if (!neg || type == EX_ERROR)
    return type;
  if (type != EX_NUMERIC)
    {
      free_node (*n);
      msg (SE, gettext (typ));
      return 0;
    }

  *n = allocate_nonterminal (OP_NEG, *n);
  return EX_NUMERIC;
}

static int
parse_exp (union any_node **n)
{
  static const char typ[]
    = N_("Both operands to the ** operator must be numeric.");

  union any_node *c;
  int type;

  type = parse_primary (n);
  if (type == EX_ERROR || token != T_EXP)
    return type;
  if (type != EX_NUMERIC)
    {
      free_node (*n);
      msg (SE, gettext (typ));
      return 0;
    }

  for (;;)
    {
      c = allocate_nonterminal (OP_POW, *n);
      lex_get ();

      type = parse_primary (n);
      if (type == EX_ERROR)
	goto fail;
      else if (type != EX_NUMERIC)
	{
	  msg (SE, gettext (typ));
	  goto fail;
	}
      *n = append_nonterminal_arg (c, *n);

      if (token != T_EXP)
	break;
    }
  return EX_NUMERIC;

fail:
  free_node (c);
  return EX_ERROR;
}

/* Parses system variables. */
static int
parse_sysvar (union any_node **n)
{
  if (!strcmp (tokid, "$CASENUM"))
    {
      *n = xmalloc (sizeof (struct casenum_node));
      (*n)->casenum.type = OP_CASENUM;
      return EX_NUMERIC;
    }
  else
    {
      double d;

      if (!strcmp (tokid, "$SYSMIS"))
	d = SYSMIS;
      else if (!strcmp (tokid, "$JDATE"))
	{
	  struct tm *time = localtime (&last_vfm_invocation);
	  d = yrmoda (time->tm_year + 1900, time->tm_mon + 1, time->tm_mday);
	}
      else if (!strcmp (tokid, "$DATE"))
	{
	  static const char *months[12] =
	    {
	      "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
	      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
	    };

	  struct tm *time;
	  char temp_buf[10];

	  time = localtime (&last_vfm_invocation);
	  sprintf (temp_buf, "%02d %s %02d", abs (time->tm_mday) % 100,
		   months[abs (time->tm_mon) % 12], abs (time->tm_year) % 100);

	  *n = xmalloc (sizeof (struct str_con_node) + 8);
	  (*n)->str_con.type = OP_STR_CON;
	  (*n)->str_con.len = 9;
	  memcpy ((*n)->str_con.s, temp_buf, 9);
	  return EX_STRING;
	}
      else if (!strcmp (tokid, "$TIME"))
	{
	  struct tm *time;
	  time = localtime (&last_vfm_invocation);
	  d = (yrmoda (time->tm_year + 1900, time->tm_mon + 1,
		       time->tm_mday) * 60. * 60. * 24.
	       + time->tm_hour * 60 * 60.
	       + time->tm_min * 60.
	       + time->tm_sec);
	}
      else if (!strcmp (tokid, "$LENGTH"))
	{
	  msg (SW, _("Use of $LENGTH is obsolete, returning default of 66."));
	  d = 66.0;
	}
      else if (!strcmp (tokid, "$WIDTH"))
	{
	  msg (SW, _("Use of $WIDTH is obsolete, returning default of 131."));
	  d = 131.0;
	}
      else
	{
	  msg (SE, _("Unknown system variable %s."), tokid);
	  return EX_ERROR;
	}

      *n = xmalloc (sizeof (struct num_con_node));
      (*n)->num_con.type = OP_NUM_CON;
      (*n)->num_con.value = d;
      return EX_NUMERIC;
    }
}

/* Parses numbers, varnames, etc. */
static int
parse_primary (union any_node **n)
{
  switch (token)
    {
    case T_ID:
      {
	struct variable *v;

	/* An identifier followed by a left parenthesis is a function
	   call. */
	if (lex_look_ahead () == '(')
	  return parse_function (n);

	/* $ at the beginning indicates a system variable. */
	if (tokid[0] == '$')
	  {
	    int type = parse_sysvar (n);
	    lex_get ();
	    return type;
	  }

	/* Otherwise, it must be a user variable. */
	v = dict_lookup_var (default_dict, tokid);
	lex_get ();
	if (v == NULL)
	  {
	    lex_error (_("expecting variable name"));
	    return EX_ERROR;
	  }

	*n = xmalloc (sizeof (struct var_node));
	(*n)->var.type = v->type == NUMERIC ? OP_NUM_VAR : OP_STR_VAR;
	(*n)->var.v = v;
	return v->type == NUMERIC ? EX_NUMERIC : EX_STRING;
      }

    case T_NUM:
      *n = xmalloc (sizeof (struct num_con_node));
      (*n)->num_con.type = OP_NUM_CON;
      (*n)->num_con.value = tokval;
      lex_get ();
      return EX_NUMERIC;

    case T_STRING:
      {
	*n = xmalloc (sizeof (struct str_con_node) + ds_length (&tokstr) - 1);
	(*n)->str_con.type = OP_STR_CON;
	(*n)->str_con.len = ds_length (&tokstr);
	memcpy ((*n)->str_con.s, ds_value (&tokstr), ds_length (&tokstr));
	lex_get ();
	return EX_STRING;
      }

    case '(':
      {
	int t;
	lex_get ();
	t = parse_or (n);
	if (!lex_match (')'))
	  {
	    lex_error (_("expecting `)'"));
	    free_node (*n);
	    return EX_ERROR;
	  }
	return t;
      }

    default:
      lex_error (_("in expression"));
      return EX_ERROR;
    }
}

/* Individual function parsing. */

struct function
  {
    const char *s;
    int t;
    int (*func) (struct function *, int, union any_node **);
    const char *desc;
  };

static struct function func_tab[];
static int func_count;

static int get_num_args (struct function *, int, union any_node **);

static int
unary_func (struct function * f, int x UNUSED, union any_node ** n)
{
  double divisor;
  struct nonterm_node *c;

  if (!get_num_args (f, 1, n))
    return EX_ERROR;

  switch (f->t)
    {
    case OP_CTIME_DAYS:
      divisor = 1 / 60. / 60. / 24.;
      goto multiply;
    case OP_CTIME_HOURS:
      divisor = 1 / 60. / 60.;
      goto multiply;
    case OP_CTIME_MINUTES:
      divisor = 1 / 60.;
      goto multiply;
    case OP_TIME_DAYS:
      divisor = 60. * 60. * 24.;
      goto multiply;

    case OP_CTIME_SECONDS:
      c = &(*n)->nonterm;
      *n = (*n)->nonterm.arg[0];
      free (c);
      return EX_NUMERIC;
    }
  return EX_NUMERIC;

multiply:
  /* Arrive here when we encounter an operation that is just a
     glorified version of a multiplication or division.  Converts the
     operation directly into that multiplication. */
  c = xmalloc (sizeof (struct nonterm_node) + sizeof (union any_node *));
  c->type = OP_MUL;
  c->n = 2;
  c->arg[0] = (*n)->nonterm.arg[0];
  c->arg[1] = xmalloc (sizeof (struct num_con_node));
  c->arg[1]->num_con.type = OP_NUM_CON;
  c->arg[1]->num_con.value = divisor;
  free (*n);
  *n = (union any_node *) c;
  return EX_NUMERIC;
}

static int
binary_func (struct function * f, int x UNUSED, union any_node ** n)
{
  if (!get_num_args (f, 2, n))
    return EX_ERROR;
  return EX_NUMERIC;
}

static int
ternary_func (struct function * f, int x UNUSED, union any_node ** n)
{
  if (!get_num_args (f, 3, n))
    return EX_ERROR;
  return EX_NUMERIC;
}

static int
MISSING_func (struct function * f, int x UNUSED, union any_node ** n)
{
  if (token == T_ID
      && dict_lookup_var (default_dict, tokid) != NULL
      && lex_look_ahead () == ')')
    {
      struct var_node *c = xmalloc (sizeof *c);
      c->v = parse_variable ();
      c->type = c->v->type == ALPHA ? OP_STR_MIS : OP_NUM_SYS;
      *n = (union any_node *) c;
      return EX_BOOLEAN;
    }
  if (!get_num_args (f, 1, n))
    return EX_ERROR;
  return EX_BOOLEAN;
}

static int
SYSMIS_func (struct function * f UNUSED, int x UNUSED, union any_node ** n)
{
  int t;
  
  if (token == T_ID
      && dict_lookup_var (default_dict, tokid)
      && lex_look_ahead () == ')')
    {
      struct variable *v;
      v = parse_variable ();
      if (v->type == ALPHA)
	{
	  struct num_con_node *c = xmalloc (sizeof *c);
	  c->type = OP_NUM_CON;
	  c->value = 0;
	  return EX_BOOLEAN;
	}
      else
	{
	  struct var_node *c = xmalloc (sizeof *c);
	  c->type = OP_NUM_SYS;
	  c->v = v;
	  return EX_BOOLEAN;
	}
    }
  
  t = parse_or (n);
  if (t == EX_ERROR)
    return t;
  else if (t == EX_NUMERIC)
    {
      *n = allocate_nonterminal (OP_SYSMIS, *n);
      return EX_BOOLEAN;
    }
  else /* EX_STRING or EX_BOOLEAN */
    {
      /* Return constant `true' value. */
      free_node (*n);
      *n = xmalloc (sizeof (struct num_con_node));
      (*n)->num_con.type = OP_NUM_CON;
      (*n)->num_con.value = 1.0;
      return EX_BOOLEAN;
    }
}

static int
VALUE_func (struct function *f UNUSED, int x UNUSED, union any_node **n)
{
  struct variable *v = parse_variable ();

  if (!v)
    return EX_ERROR;
  *n = xmalloc (sizeof (struct var_node));
  (*n)->var.v = v;
  if (v->type == NUMERIC)
    {
      (*n)->var.type = OP_NUM_VAL;
      return EX_NUMERIC;
    }
  else
    {
      (*n)->var.type = OP_STR_VAR;
      return EX_STRING;
    }
}

static int
LAG_func (struct function *f UNUSED, int x UNUSED, union any_node **n)
{
  struct variable *v = parse_variable ();
  int nlag = 1;

  if (!v)
    return EX_ERROR;
  if (lex_match (','))
    {
      if (!lex_integer_p () || lex_integer () <= 0 || lex_integer () > 1000)
	{
	  msg (SE, _("Argument 2 to LAG must be a small positive "
		     "integer constant."));
	  return 0;
	}
      
      nlag = lex_integer ();
      lex_get ();
    }
  n_lag = max (nlag, n_lag);
  *n = xmalloc (sizeof (struct lag_node));
  (*n)->lag.type = (v->type == NUMERIC ? OP_NUM_LAG : OP_STR_LAG);
  (*n)->lag.v = v;
  (*n)->lag.lag = nlag;
  return (v->type == NUMERIC ? EX_NUMERIC : EX_STRING);
}

/* This screwball function parses n-ary operators:
   1. NMISS, NVALID, SUM, MEAN, MIN, MAX: any number of (numeric) arguments.
   2. SD, VARIANCE, CFVAR: at least two (numeric) arguments.
   3. RANGE: An odd number of arguments, but at least three.
   All arguments must be the same type.
   4. ANY: At least two arguments.  All arguments must be the same type.
 */
static int
nary_num_func (struct function *f, int min_args, union any_node **n)
{
  /* Argument number of current argument (used for error messages). */
  int argn = 1;

  /* Number of arguments. */
  int nargs;

  /* Number of arguments allocated. */
  int m = 16;

  /* Type of arguments. */
  int type = (f->t == OP_ANY || f->t == OP_RANGE) ? -1 : NUMERIC;

  *n = xmalloc (sizeof (struct nonterm_node) + sizeof (union any_node *[15]));
  (*n)->nonterm.type = f->t;
  (*n)->nonterm.n = 0;
  for (;;)
    {
      /* Special case: vara TO varb. */

      /* FIXME: Is this condition failsafe?  Can we _ever_ have two
         juxtaposed identifiers otherwise?  */
      if (token == T_ID && dict_lookup_var (default_dict, tokid) != NULL
	  && toupper (lex_look_ahead ()) == 'T')
	{
	  struct variable **v;
	  int nv;
	  int j;
	  int opts = PV_SINGLE;

	  if (type == NUMERIC)
	    opts |= PV_NUMERIC;
	  else if (type == ALPHA)
	    opts |= PV_STRING;
	  if (!parse_variables (default_dict, &v, &nv, opts))
	    goto fail;
	  if (nv + (*n)->nonterm.n >= m)
	    {
	      m += nv + 16;
	      *n = xrealloc (*n, (sizeof (struct nonterm_node)
				  + (m - 1) * sizeof (union any_node *)));
	    }
	  if (type == -1)
	    {
	      type = v[0]->type;
	      for (j = 1; j < nv; j++)
		if (type != v[j]->type)
		  {
		    msg (SE, _("Type mismatch in argument %d of %s, which was "
			       "expected to be of %s type.  It was actually "
			       "of %s type. "),
			 argn, f->s, type_name (type), type_name (v[j]->type));
		    free (v);
		    goto fail;
		  }
	    }
	  for (j = 0; j < nv; j++)
	    {
	      union any_node **c = &(*n)->nonterm.arg[(*n)->nonterm.n++];
	      *c = xmalloc (sizeof (struct var_node));
	      (*c)->var.type = (type == NUMERIC ? OP_NUM_VAR : OP_STR_VAR);
	      (*c)->var.v = v[j];
	    }
	}
      else
	{
	  union any_node *c;
	  int t = parse_or (&c);

	  if (t == EX_ERROR)
	    goto fail;
	  if (t == EX_BOOLEAN)
	    {
	      free_node (c);
	      msg (SE, _("%s cannot take Boolean operands."), f->s);
	      goto fail;
	    }
	  if (type == -1)
	    {
	      if (t == EX_NUMERIC)
		type = NUMERIC;
	      else if (t == EX_STRING)
		type = ALPHA;
	    }
	  else if ((t == EX_NUMERIC) ^ (type == NUMERIC))
	    {
	      free_node (c);
	      msg (SE, _("Type mismatch in argument %d of %s, which was "
			 "expected to be of %s type.  It was actually "
			 "of %s type. "),
		   argn, f->s, type_name (type), expr_type_name (t));
	      goto fail;
	    }
	  if ((*n)->nonterm.n + 1 >= m)
	    {
	      m += 16;
	      *n = xrealloc (*n, (sizeof (struct nonterm_node)
				  + (m - 1) * sizeof (union any_node *)));
	    }
	  (*n)->nonterm.arg[(*n)->nonterm.n++] = c;
	}

      if (token == ')')
	break;
      if (!lex_match (','))
	{
	  lex_error (_("in function call"));
	  goto fail;
	}

      argn++;
    }
  *n = xrealloc (*n, (sizeof (struct nonterm_node)
		      + ((*n)->nonterm.n) * sizeof (union any_node *)));

  nargs = (*n)->nonterm.n;
  if (f->t == OP_RANGE)
    {
      if (nargs < 3 || (nargs & 1) == 0)
	{
	  msg (SE, _("RANGE requires an odd number of arguments, but "
		     "at least three."));
	  return 0;
	}
    }
  else if (f->t == OP_SD || f->t == OP_VARIANCE
	   || f->t == OP_CFVAR || f->t == OP_ANY)
    {
      if (nargs < 2)
	{
	  msg (SE, _("%s requires at least two arguments."), f->s);
	  return 0;
	}
    }

  if (f->t == OP_CFVAR || f->t == OP_SD || f->t == OP_VARIANCE)
    min_args = max (min_args, 2);
  else
    min_args = max (min_args, 1);

  /* Yes, this is admittedly a terrible crock, but it works. */
  (*n)->nonterm.arg[(*n)->nonterm.n] = (union any_node *) min_args;

  if (min_args > nargs)
    {
      msg (SE, _("%s.%d requires at least %d arguments."),
	   f->s, min_args, min_args);
      return 0;
    }

  if (f->t == OP_ANY || f->t == OP_RANGE)
    {
      if (type == T_STRING)
	f->t++;
      return EX_BOOLEAN;
    }
  else
    return EX_NUMERIC;

fail:
  free_node (*n);
  return EX_ERROR;
}

static int
CONCAT_func (struct function * f UNUSED, int x UNUSED, union any_node ** n)
{
  int m = 0;

  int type;

  *n = xmalloc (sizeof (struct nonterm_node) + sizeof (union any_node *[15]));
  (*n)->nonterm.type = OP_CONCAT;
  (*n)->nonterm.n = 0;
  for (;;)
    {
      if ((*n)->nonterm.n >= m)
	{
	  m += 16;
	  *n = xrealloc (*n, (sizeof (struct nonterm_node)
			      + (m - 1) * sizeof (union any_node *)));
	}
      type = parse_or (&(*n)->nonterm.arg[(*n)->nonterm.n]);
      if (type == EX_ERROR)
	goto fail;
      if (type != EX_STRING)
	{
	  msg (SE, _("Argument %d to CONCAT is type %s.  All arguments "
		     "to CONCAT must be strings."),
	       (*n)->nonterm.n + 1, expr_type_name (type));
	  goto fail;
	}
      (*n)->nonterm.n++;

      if (!lex_match (','))
	break;
    }
  *n = xrealloc (*n, (sizeof (struct nonterm_node)
		      + ((*n)->nonterm.n - 1) * sizeof (union any_node *)));
  return EX_STRING;

fail:
  free_node (*n);
  return EX_ERROR;
}

/* Parses a string function according to f->desc.  f->desc[0] is the
   return type of the function.  Succeeding characters represent
   successive args.  Optional args are separated from the required
   args by a slash (`/').  Codes are `n', numeric arg; `s', string
   arg; and `f', format spec (this must be the last arg).  If the
   optional args are included, the type becomes f->t+1. */
static int
generic_str_func (struct function *f, int x UNUSED, union any_node ** n)
{
  int max_args = 0;
  int type;
  const char *cp;

  /* Count max number of arguments. */
  cp = &f->desc[1];
  while (*cp)
    {
      if (*cp == 'n' || *cp == 's')
	max_args++;
      else if (*cp == 'f')
	max_args += 3;
      cp++;
    }
  cp = &f->desc[1];

  *n = xmalloc (sizeof (struct nonterm_node)
		+ (max_args - 1) * sizeof (union any_node *));
  (*n)->nonterm.type = f->t;
  (*n)->nonterm.n = 0;
  for (;;)
    {
      if (*cp == 'n' || *cp == 's')
	{
	  int t = *cp == 'n' ? EX_NUMERIC : EX_STRING;
	  type = parse_or (&(*n)->nonterm.arg[(*n)->nonterm.n]);

	  if (type == EX_ERROR)
	    goto fail;
	  if (type != t)
	    {
	      msg (SE, _("Argument %d to %s was expected to be of %s type.  "
			 "It was actually of type %s."),
		   (*n)->nonterm.n + 1, f->s,
		   *cp == 'n' ? _("numeric") : _("string"),
		   expr_type_name (type));
	      goto fail;
	    }
	  (*n)->nonterm.n++;
	}
      else if (*cp == 'f')
	{
	  /* This is always the very last argument.  Also, this code
	     is a crock.  However, it works. */
	  struct fmt_spec fmt;

	  if (!parse_format_specifier (&fmt, 0))
	    goto fail;
	  if (formats[fmt.type].cat & FCAT_STRING)
	    {
	      msg (SE, _("%s is not a numeric format."), fmt_to_string (&fmt));
	      goto fail;
	    }
	  (*n)->nonterm.arg[(*n)->nonterm.n + 0] = (union any_node *) fmt.type;
	  (*n)->nonterm.arg[(*n)->nonterm.n + 1] = (union any_node *) fmt.w;
	  (*n)->nonterm.arg[(*n)->nonterm.n + 2] = (union any_node *) fmt.d;
	  break;
	}
      else
	assert (0);

      if (*++cp == 0)
	break;
      if (*cp == '/')
	{
	  cp++;
	  if (lex_match (','))
	    {
	      (*n)->nonterm.type++;
	      continue;
	    }
	  else
	    break;
	}
      else if (!lex_match (','))
	{
	  msg (SE, _("Too few arguments to function %s."), f->s);
	  goto fail;
	}
    }

  return f->desc[0] == 'n' ? EX_NUMERIC : EX_STRING;

fail:
  free_node (*n);
  return EX_ERROR;
}

/* General function parsing. */

static int
get_num_args (struct function *f, int num_args, union any_node **n)
{
  int t;
  int i;

  *n = xmalloc (sizeof (struct nonterm_node)
		+ (num_args - 1) * sizeof (union any_node *));
  (*n)->nonterm.type = f->t;
  (*n)->nonterm.n = 0;
  for (i = 0;;)
    {
      t = parse_or (&(*n)->nonterm.arg[i]);
      if (t == EX_ERROR)
	goto fail;
      (*n)->nonterm.n++;
      if (t != EX_NUMERIC)
	{
	  msg (SE, _("Type mismatch in argument %d of %s, which was expected "
		     "to be numeric.  It was actually type %s."),
	       i + 1, f->s, expr_type_name (t));
	  goto fail;
	}
      if (++i >= num_args)
	return 1;
      if (!lex_match (','))
	{
	  msg (SE, _("Missing comma following argument %d of %s."), i + 1, f->s);
	  goto fail;
	}
    }

fail:
  free_node (*n);
  return 0;
}

static int
parse_function (union any_node ** n)
{
  struct function *fp;
  char fname[32], *cp;
  int t;
  int min_args;
  const struct vector *v;

  /* Check for a vector with this name. */
  v = dict_lookup_vector (default_dict, tokid);
  if (v)
    {
      lex_get ();
      assert (token == '(');
      lex_get ();

      *n = xmalloc (sizeof (struct nonterm_node)
		    + sizeof (union any_node *[2]));
      (*n)->nonterm.type = (v->var[0]->type == NUMERIC
			? OP_VEC_ELEM_NUM : OP_VEC_ELEM_STR);
      (*n)->nonterm.n = 0;

      t = parse_or (&(*n)->nonterm.arg[0]);
      if (t == EX_ERROR)
	goto fail;
      if (t != EX_NUMERIC)
	{
	  msg (SE, _("The index value after a vector name must be numeric."));
	  goto fail;
	}
      (*n)->nonterm.n++;

      if (!lex_match (')'))
	{
	  msg (SE, _("`)' expected after a vector index value."));
	  goto fail;
	}
      ((*n)->nonterm.arg[1]) = (union any_node *) v->idx;

      return v->var[0]->type == NUMERIC ? EX_NUMERIC : EX_STRING;
    }

  ds_truncate (&tokstr, 31);
  strcpy (fname, ds_value (&tokstr));
  cp = strrchr (fname, '.');
  if (cp && isdigit ((unsigned char) cp[1]))
    {
      min_args = atoi (&cp[1]);
      *cp = 0;
    }
  else
    min_args = 0;

  lex_get ();
  if (!lex_force_match ('('))
    return 0;
  
  {
    struct function f;
    f.s = fname;
    
    fp = binary_search (func_tab, func_count, sizeof *func_tab, &f,
                        compare_functions, NULL);
  }
  
  if (!fp)
    {
      msg (SE, _("There is no function named %s."), fname);
      return 0;
    }
  if (min_args && fp->func != nary_num_func)
    {
      msg (SE, _("Function %s may not be given a minimum number of "
		 "arguments."), fname);
      return 0;
    }
  t = fp->func (fp, min_args, n);
  if (t == EX_ERROR)
    return EX_ERROR;
  if (!lex_match (')'))
    {
      lex_error (_("expecting `)' after %s function"), fname);
      goto fail;
    }

  return t;

fail:
  free_node (*n);
  return EX_ERROR;
}

#if GLOBAL_DEBUGGING
#define op(a,b,c,d) {a,b,c,d}
#else
#define op(a,b,c,d) {b,c,d}
#endif

#define varies 0

struct op_desc ops[OP_SENTINEL + 1] =
{
  op ("!?ERROR?!", 000, 0, 0),

  op ("plus", 001, varies, 1),
  op ("mul", 011, varies, 1),
  op ("pow", 010, -1, 0),
  op ("and", 010, -1, 0),
  op ("or", 010, -1, 0),
  op ("not", 000, 0, 0),
  op ("eq", 000, -1, 0),
  op ("ge", 000, -1, 0),
  op ("gt", 000, -1, 0),
  op ("le", 000, -1, 0),
  op ("lt", 000, -1, 0),
  op ("ne", 000, -1, 0),

  op ("string-eq", 000, -1, 0),
  op ("string-ge", 000, -1, 0),
  op ("string-gt", 000, -1, 0),
  op ("string-le", 000, -1, 0),
  op ("string-lt", 000, -1, 0),
  op ("string-ne", 000, -1, 0),

  op ("neg", 000, 0, 0),
  op ("abs", 000, 0, 0),
  op ("arcos", 000, 0, 0),
  op ("arsin", 000, 0, 0),
  op ("artan", 000, 0, 0),
  op ("cos", 000, 0, 0),
  op ("exp", 000, 0, 0),
  op ("lg10", 000, 0, 0),
  op ("ln", 000, 0, 0),
  op ("mod10", 000, 0, 0),
  op ("rnd", 000, 0, 0),
  op ("sin", 000, 0, 0),
  op ("sqrt", 000, 0, 0),
  op ("tan", 000, 0, 0),
  op ("trunc", 000, 0, 0),

  op ("any", 011, varies, 1),
  op ("any-string", 001, varies, 1),
  op ("cfvar", 013, varies, 2),
  op ("max", 013, varies, 2),
  op ("mean", 013, varies, 2),
  op ("min", 013, varies, 2),
  op ("nmiss", 011, varies, 1),
  op ("nvalid", 011, varies, 1),
  op ("range", 011, varies, 1),
  op ("range-string", 001, varies, 1),
  op ("sd", 013, varies, 2),
  op ("sum", 013, varies, 2),
  op ("variance", 013, varies, 2),

  op ("time_hms", 000, -2, 0),
  op ("ctime_days?!", 000, 0, 0),
  op ("ctime_hours?!", 000, 0, 0),
  op ("ctime_minutes?!", 000, 0, 0),
  op ("ctime_seconds?!", 000, 0, 0),
  op ("time_days?!", 000, 0, 0),

  op ("date_dmy", 000, -2, 0),
  op ("date_mdy", 000, -2, 0),
  op ("date_moyr", 000, -1, 0),
  op ("date_qyr", 000, -1, 0),
  op ("date_wkyr", 000, -1, 0),
  op ("date_yrday", 000, -1, 0),
  op ("yrmoda", 000, -2, 0),

  op ("xdate_date", 000, 0, 0),
  op ("xdate_hour", 000, 0, 0),
  op ("xdate_jday", 000, 0, 0),
  op ("xdate_mday", 000, 0, 0),
  op ("xdate_minute", 000, 0, 0),
  op ("xdate_month", 000, 0, 0),
  op ("xdate_quarter", 000, 0, 0),
  op ("xdate_second", 000, 0, 0),
  op ("xdate_tday", 000, 0, 0),
  op ("xdate_time", 000, 0, 0),
  op ("xdate_week", 000, 0, 0),
  op ("xdate_wkday", 000, 0, 0),
  op ("xdate_year", 000, 0, 0),

  op ("concat", 001, varies, 1),
  op ("index-2", 000, -1, 0),
  op ("index-3", 000, -2, 0),
  op ("rindex-2", 000, -1, 0),
  op ("rindex-3", 000, -2, 0),
  op ("length", 000, 0, 0),
  op ("lower", 000, 0, 0),
  op ("upcas", 000, 0, 0),
  op ("lpad-2", 010, -1, 0),
  op ("lpad-3", 010, -2, 0),
  op ("rpad-2", 010, -1, 0),
  op ("rpad-3", 010, -2, 0),
  op ("ltrim-1", 000, 0, 0),
  op ("ltrim-2", 000, -1, 0),
  op ("rtrim-1", 000, 0, 0),
  op ("rtrim-2", 000, -1, 0),
  op ("number-1", 010, 0, 0),
  op ("number-2", 014, 0, 3),
  op ("string", 004, 0, 3),
  op ("substr-2", 010, -1, 0),
  op ("substr-3", 010, -2, 0),

  op ("inv", 000, 0, 0),
  op ("square", 000, 0, 0),
  op ("num-to-Bool", 000, 0, 0),

  op ("mod", 010, -1, 0),
  op ("normal", 000, 0, 0),
  op ("uniform", 000, 0, 0),
  op ("sysmis", 010, 0, 0),
  op ("vec-elem-num", 002, 0, 1),
  op ("vec-elem-str", 002, 0, 1),

  op ("!?TERMINAL?!", 000, 0, 0),
  op ("num-con", 000, +1, 0),
  op ("str-con", 000, +1, 0),
  op ("num-var", 000, +1, 0),
  op ("str-var", 000, +1, 0),
  op ("num-lag", 000, +1, 1),
  op ("str-lag", 000, +1, 1),
  op ("num-sys", 000, +1, 1),
  op ("num-val", 000, +1, 1),
  op ("str-mis", 000, +1, 1),
  op ("$casenum", 000, +1, 0),
  op ("!?SENTINEL?!", 000, 0, 0),
};

#undef op
#undef varies


/* Utility functions. */

static const char *
expr_type_name (int type)
{
  switch (type)
    {
    case EX_ERROR:
      return _("error");

    case EX_BOOLEAN:
      return _("Boolean");

    case EX_NUMERIC:
      return _("numeric");

    case EX_STRING:
      return _("string");

    default:
      assert (0);
      return 0;
    }
}

static const char *
type_name (int type)
{
  switch (type)
    {
    case NUMERIC:
      return _("numeric");
    case ALPHA:
      return _("string");
    default:
      assert (0);
      return 0;
    }
}

static void
make_bool (union any_node **n)
{
  union any_node *c;

  c = xmalloc (sizeof (struct nonterm_node));
  c->nonterm.type = OP_NUM_TO_BOOL;
  c->nonterm.n = 1;
  c->nonterm.arg[0] = *n;
  *n = c;
}

void
free_node (union any_node *n)
{
  if (n->type < OP_TERMINAL)
    {
      int i;

      for (i = 0; i < n->nonterm.n; i++)
	free_node (n->nonterm.arg[i]);
    }
  free (n);
}

union any_node *
allocate_nonterminal (int op, union any_node *n)
{
  union any_node *c;

  c = xmalloc (sizeof c->nonterm);
  c->nonterm.type = op;
  c->nonterm.n = 1;
  c->nonterm.arg[0] = n;

  return c;
}

union any_node *
append_nonterminal_arg (union any_node *a, union any_node *b)
{
  a = xrealloc (a, sizeof *a + sizeof *a->nonterm.arg * a->nonterm.n);
  a->nonterm.arg[a->nonterm.n++] = b;
  return a;
}

static struct function func_tab[] =
{
  {"ABS", OP_ABS, unary_func, NULL},
  {"ACOS", OP_ARCOS, unary_func, NULL},
  {"ARCOS", OP_ARCOS, unary_func, NULL},
  {"ARSIN", OP_ARSIN, unary_func, NULL},
  {"ARTAN", OP_ARTAN, unary_func, NULL},
  {"ASIN", OP_ARSIN, unary_func, NULL},
  {"ATAN", OP_ARTAN, unary_func, NULL},
  {"COS", OP_COS, unary_func, NULL},
  {"EXP", OP_EXP, unary_func, NULL},
  {"LG10", OP_LG10, unary_func, NULL},
  {"LN", OP_LN, unary_func, NULL},
  {"MOD10", OP_MOD10, unary_func, NULL},
  {"NORMAL", OP_NORMAL, unary_func, NULL},
  {"RND", OP_RND, unary_func, NULL},
  {"SIN", OP_SIN, unary_func, NULL},
  {"SQRT", OP_SQRT, unary_func, NULL},
  {"TAN", OP_TAN, unary_func, NULL},
  {"TRUNC", OP_TRUNC, unary_func, NULL},
  {"UNIFORM", OP_UNIFORM, unary_func, NULL},

  {"TIME.DAYS", OP_TIME_DAYS, unary_func, NULL},
  {"TIME.HMS", OP_TIME_HMS, ternary_func, NULL},

  {"CTIME.DAYS", OP_CTIME_DAYS, unary_func, NULL},
  {"CTIME.HOURS", OP_CTIME_HOURS, unary_func, NULL},
  {"CTIME.MINUTES", OP_CTIME_MINUTES, unary_func, NULL},
  {"CTIME.SECONDS", OP_CTIME_SECONDS, unary_func, NULL},

  {"DATE.DMY", OP_DATE_DMY, ternary_func, NULL},
  {"DATE.MDY", OP_DATE_MDY, ternary_func, NULL},
  {"DATE.MOYR", OP_DATE_MOYR, binary_func, NULL},
  {"DATE.QYR", OP_DATE_QYR, binary_func, NULL},
  {"DATE.WKYR", OP_DATE_WKYR, binary_func, NULL},
  {"DATE.YRDAY", OP_DATE_YRDAY, binary_func, NULL},

  {"XDATE.DATE", OP_XDATE_DATE, unary_func, NULL},
  {"XDATE.HOUR", OP_XDATE_HOUR, unary_func, NULL},
  {"XDATE.JDAY", OP_XDATE_JDAY, unary_func, NULL},
  {"XDATE.MDAY", OP_XDATE_MDAY, unary_func, NULL},
  {"XDATE.MINUTE", OP_XDATE_MINUTE, unary_func, NULL},
  {"XDATE.MONTH", OP_XDATE_MONTH, unary_func, NULL},
  {"XDATE.QUARTER", OP_XDATE_QUARTER, unary_func, NULL},
  {"XDATE.SECOND", OP_XDATE_SECOND, unary_func, NULL},
  {"XDATE.TDAY", OP_XDATE_TDAY, unary_func, NULL},
  {"XDATE.TIME", OP_XDATE_TIME, unary_func, NULL},
  {"XDATE.WEEK", OP_XDATE_WEEK, unary_func, NULL},
  {"XDATE.WKDAY", OP_XDATE_WKDAY, unary_func, NULL},
  {"XDATE.YEAR", OP_XDATE_YEAR, unary_func, NULL},

  {"MISSING", OP_SYSMIS, MISSING_func, NULL},
  {"MOD", OP_MOD, binary_func, NULL},
  {"SYSMIS", OP_SYSMIS, SYSMIS_func, NULL},
  {"VALUE", OP_NUM_VAL, VALUE_func, NULL},
  {"LAG", OP_NUM_LAG, LAG_func, NULL},
  {"YRMODA", OP_YRMODA, ternary_func, NULL},

  {"ANY", OP_ANY, nary_num_func, NULL},
  {"CFVAR", OP_CFVAR, nary_num_func, NULL},
  {"MAX", OP_MAX, nary_num_func, NULL},
  {"MEAN", OP_MEAN, nary_num_func, NULL},
  {"MIN", OP_MIN, nary_num_func, NULL},
  {"NMISS", OP_NMISS, nary_num_func, NULL},
  {"NVALID", OP_NVALID, nary_num_func, NULL},
  {"RANGE", OP_RANGE, nary_num_func, NULL},
  {"SD", OP_SD, nary_num_func, NULL},
  {"SUM", OP_SUM, nary_num_func, NULL},
  {"VARIANCE", OP_VARIANCE, nary_num_func, NULL},

  {"CONCAT", OP_CONCAT, CONCAT_func, NULL},
  {"INDEX", OP_INDEX, generic_str_func, "nss/n"},
  {"RINDEX", OP_RINDEX, generic_str_func, "nss/n"},
  {"LENGTH", OP_LENGTH, generic_str_func, "ns"},
  {"LOWER", OP_LOWER, generic_str_func, "ss"},
  {"UPCAS", OP_UPPER, generic_str_func, "ss"},
  {"LPAD", OP_LPAD, generic_str_func, "ssn/s"},
  {"RPAD", OP_RPAD, generic_str_func, "ssn/s"},
  {"LTRIM", OP_LTRIM, generic_str_func, "ss/s"},
  {"RTRIM", OP_RTRIM, generic_str_func, "ss/s"},
  {"NUMBER", OP_NUMBER, generic_str_func, "ns/f"},
  {"STRING", OP_STRING, generic_str_func, "snf"},
  {"SUBSTR", OP_SUBSTR, generic_str_func, "ssn/n"},
};

/* An algo_compare_func that compares functions A and B based on
   their names. */
static int
compare_functions (const void *a_, const void *b_, void *aux UNUSED)
{
  const struct function *a = a_;
  const struct function *b = b_;

  return strcmp (a->s, b->s);
}

static void
init_func_tab (void)
{
  {
    static int inited;

    if (inited)
      return;
    inited = 1;
  }

  func_count = sizeof func_tab / sizeof *func_tab;
  sort (func_tab, func_count, sizeof *func_tab, compare_functions, NULL);
}

/* Debug output. */

#if DEBUGGING
static void
print_type (union any_node * n)
{
  const char *s;
  size_t len;

  s = ops[n->type].name;
  len = strlen (s);
  if (ops[n->type].flags & OP_MIN_ARGS)
    printf ("%s.%d\n", s, (int) n->nonterm.arg[n->nonterm.n]);
  else if (ops[n->type].flags & OP_FMT_SPEC)
    {
      struct fmt_spec f;

      f.type = (int) n->nonterm.arg[n->nonterm.n + 0];
      f.w = (int) n->nonterm.arg[n->nonterm.n + 1];
      f.d = (int) n->nonterm.arg[n->nonterm.n + 2];
      printf ("%s(%s)\n", s, fmt_to_string (&f));
    }
  else
    printf ("%s\n", s);
}

static void
debug_print_tree (union any_node * n, int level)
{
  int i;
  for (i = 0; i < level; i++)
    printf ("  ");
  if (n->type < OP_TERMINAL)
    {
      print_type (n);
      for (i = 0; i < n->nonterm.n; i++)
	debug_print_tree (n->nonterm.arg[i], level + 1);
    }
  else
    {
      switch (n->type)
	{
	case OP_TERMINAL:
	  printf (_("!!TERMINAL!!"));
	  break;
	case OP_NUM_CON:
	  if (n->num_con.value == SYSMIS)
	    printf ("SYSMIS");
	  else
	    printf ("%f", n->num_con.value);
	  break;
	case OP_STR_CON:
	  printf ("\"%.*s\"", n->str_con.len, n->str_con.s);
	  break;
	case OP_NUM_VAR:
	case OP_STR_VAR:
	  printf ("%s", n->var.v->name);
	  break;
	case OP_NUM_LAG:
	case OP_STR_LAG:
	  printf ("LAG(%s,%d)", n->lag.v->name, n->lag.lag);
	  break;
	case OP_NUM_SYS:
	  printf ("SYSMIS(%s)", n->var.v->name);
	  break;
	case OP_NUM_VAL:
	  printf ("VALUE(%s)", n->var.v->name);
	  break;
	case OP_SENTINEL:
	  printf (_("!!SENTINEL!!"));
	  break;
	default:
	  printf (_("!!ERROR%d!!"), n->type);
	  assert (0);
	}
      printf ("\n");
    }
}
#endif /* DEBUGGING */

#if GLOBAL_DEBUGGING
static void
debug_print_postfix (struct expression * e)
{
  unsigned char *o;
  double *num = e->num;
  unsigned char *str = e->str;
  struct variable **v = e->var;
  int t;

  debug_printf ((_("postfix:")));
  for (o = e->op; *o != OP_SENTINEL;)
    {
      t = *o++;
      if (t < OP_TERMINAL)
	{
	  debug_printf ((" %s", ops[t].name));

	  if (ops[t].flags & OP_VAR_ARGS)
	    {
	      debug_printf (("(%d)", *o));
	      o++;
	    }
	  if (ops[t].flags & OP_MIN_ARGS)
	    {
	      debug_printf ((".%d", *o));
	      o++;
	    }
	  if (ops[t].flags & OP_FMT_SPEC)
	    {
	      struct fmt_spec f;
	      f.type = (int) *o++;
	      f.w = (int) *o++;
	      f.d = (int) *o++;
	      debug_printf (("(%s)", fmt_to_string (&f)));
	    }
	}
      else if (t == OP_NUM_CON)
	{
	  if (*num == SYSMIS)
	    debug_printf ((" SYSMIS"));
	  else
	    debug_printf ((" %f", *num));
	  num++;
	}
      else if (t == OP_STR_CON)
	{
	  debug_printf ((" \"%.*s\"", *str, &str[1]));
	  str += str[0] + 1;
	}
      else if (t == OP_NUM_VAR || t == OP_STR_VAR)
	{
	  debug_printf ((" %s", (*v)->name));
	  v++;
	}
      else if (t == OP_NUM_SYS)
	{
	  debug_printf ((" SYSMIS(#%d)", *o));
	  o++;
	}
      else if (t == OP_NUM_VAL)
	{
	  debug_printf ((" VALUE(#%d)", *o));
	  o++;
	}
      else if (t == OP_NUM_LAG || t == OP_STR_LAG)
	{
	  debug_printf ((" LAG(%s,%d)", (*v)->name, *o));
	  o++;
	  v++;
	}
      else
	{
	  printf ("debug_print_postfix(): %d\n", t);
	  assert (0);
	}
    }
  debug_putc ('\n', stdout);
}
#endif /* GLOBAL_DEBUGGING */
