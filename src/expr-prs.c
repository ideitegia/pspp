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
#include "error.h"
#include <ctype.h>
#include <float.h>
#include <stdlib.h>
#include "algorithm.h"
#include "alloc.h"
#include "error.h"
#include "lexer.h"
#include "misc.h"
#include "settings.h"
#include "str.h"
#include "var.h"
#include "vfm.h"
#include "pool.h"

/* Declarations. */

/* Recursive descent parser in order of increasing precedence. */
typedef enum expr_type parse_recursively_func (union any_node **);
static parse_recursively_func parse_or, parse_and, parse_not;
static parse_recursively_func parse_rel, parse_add, parse_mul;
static parse_recursively_func parse_neg, parse_exp;
static parse_recursively_func parse_primary, parse_function;

/* Utility functions. */
static const char *expr_type_name (enum expr_type type);
static const char *var_type_name (int var_type);
static void make_bool (union any_node **n);
static union any_node *allocate_nonterminal (int op, union any_node *n);
static union any_node *allocate_binary_nonterminal (int op, union any_node *,
                                                    union any_node *);
static union any_node *allocate_num_con (double value);
static union any_node *allocate_str_con (const char *string, size_t length);
static union any_node *allocate_var_node (int type, struct variable *);
static int type_check (union any_node **n,
                       enum expr_type actual_type,
                       enum expr_type expected_type);

static algo_compare_func compare_functions;
static void init_func_tab (void);

#if DEBUGGING
static void debug_print_tree (union any_node *, int);
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
expr_parse (enum expr_type expected_type)
{
  struct expression *e;
  union any_node *n;
  enum expr_type actual_type;
  int optimize = (expected_type & EXPR_NO_OPTIMIZE) == 0;

  expected_type &= ~EXPR_NO_OPTIMIZE;

  /* Make sure the table of functions is initialized. */
  init_func_tab ();

  /* Parse the expression. */
  actual_type = parse_or (&n);
  if (actual_type == EXPR_ERROR)
    return NULL;

  /* Enforce type rules. */
  if (!type_check (&n, actual_type, expected_type))
    {
      free_node (n);
      return NULL;
    }

  /* Optimize the expression as best we can. */
  if (optimize)
    optimize_expression (&n);

  /* Dump the tree-based expression to a postfix representation for
     best evaluation speed, and destroy the tree. */
  e = xmalloc (sizeof *e);
  e->type = actual_type;
  dump_expression (n, e);
  free_node (n);

  return e;
}

/* Returns the type of EXPR. */
enum expr_type
expr_get_type (const struct expression *expr) 
{
  assert (expr != NULL);
  return expr->type;
}

static int
type_check (union any_node **n, enum expr_type actual_type, enum expr_type expected_type)
{
  switch (expected_type) 
    {
    case EXPR_BOOLEAN:
    case EXPR_NUMERIC:
      if (actual_type == EXPR_STRING)
	{
	  msg (SE, _("Type mismatch: expression has string type, "
                     "but a numeric value is required here."));
	  return 0;
	}
      if (actual_type == EXPR_NUMERIC && expected_type == EXPR_BOOLEAN)
	*n = allocate_nonterminal (OP_NUM_TO_BOOL, *n);
      break;
      
    case EXPR_STRING:
      if (actual_type != EXPR_STRING)
        {
          msg (SE, _("Type mismatch: expression has numeric type, "
                     "but a string value is required here."));
          return 0;
        }
      break;

    case EXPR_ANY:
      break;

    default:
      assert (0); 
    }
  
  return 1;
}

/* Recursive-descent expression parser. */

/* Coerces *NODE, of type ACTUAL_TYPE, to type REQUIRED_TYPE, and
   returns success.  If ACTUAL_TYPE cannot be coerced to the
   desired type then we issue an error message about operator
   OPERATOR_NAME and free *NODE. */
static int
type_coercion (enum expr_type actual_type, enum expr_type required_type,
               union any_node **node,
               const char *operator_name) 
{
  assert (required_type == EXPR_NUMERIC
          || required_type == EXPR_BOOLEAN
          || required_type == EXPR_STRING);

  if (actual_type == required_type) 
    {
      /* Type match. */
      return 1; 
    }
  else if (actual_type == EXPR_ERROR)
    {
      /* Error already reported. */
      *node = NULL;
      return 0;
    }
  else if (actual_type == EXPR_BOOLEAN && required_type == EXPR_NUMERIC) 
    {
      /* Boolean -> numeric: nothing to do. */
      return 1;
    }
  else if (actual_type == EXPR_NUMERIC && required_type == EXPR_BOOLEAN) 
    {
      /* Numeric -> Boolean: insert conversion. */
      make_bool (node);
      return 1;
    }
  else
    {
      /* We want a string and got a number/Boolean, or vice versa. */
      assert ((actual_type == EXPR_STRING) != (required_type == EXPR_STRING));

      if (required_type == EXPR_STRING)
        msg (SE, _("Type mismatch: operands of %s operator must be strings."),
             operator_name);
      else
        msg (SE, _("Type mismatch: operands of %s operator must be numeric."),
             operator_name);
      free_node (*node);
      *node = NULL;
      return 0;
    }
}

/* An operator. */
struct operator 
  {
    int token;          /* Operator token. */
    int type;           /* Operator node type. */
    const char *name;   /* Operator name. */
  };

/* Attempts to match the current token against the tokens for the
   OP_CNT operators in OPS[].  If successful, returns nonzero
   and, if OPERATOR is non-null, sets *OPERATOR to the operator.
   On failure, returns zero and, if OPERATOR is non-null, sets
   *OPERATOR to a null pointer. */
static int
match_operator (const struct operator ops[], size_t op_cnt,
                const struct operator **operator) 
{
  const struct operator *op;

  for (op = ops; op < ops + op_cnt; op++)
    {
      if (op->token == '-')
        lex_negative_to_dash ();
      if (lex_match (op->token)) 
        {
          if (operator != NULL)
            *operator = op;
          return 1;
        }
    }
  if (operator != NULL)
    *operator = NULL;
  return 0;
}

/* Parses a chain of left-associative operator/operand pairs.
   The operators' operands uniformly must be type REQUIRED_TYPE.
   There are OP_CNT operators, specified in OPS[].  The next
   higher level is parsed by PARSE_NEXT_LEVEL.  If CHAIN_WARNING
   is non-null, then it will be issued as a warning if more than
   one operator/operand pair is parsed. */
static enum expr_type
parse_binary_operators (union any_node **node,
                        enum expr_type actual_type,
                        enum expr_type required_type,
                        enum expr_type result_type,
                        const struct operator ops[], size_t op_cnt,
                        parse_recursively_func *parse_next_level,
                        const char *chain_warning)
{
  int op_count;
  const struct operator *operator;

  if (actual_type == EXPR_ERROR)
    return EXPR_ERROR;

  for (op_count = 0; match_operator (ops, op_cnt, &operator); op_count++)
    {
      union any_node *rhs;

      /* Convert the left-hand side to type REQUIRED_TYPE. */
      if (!type_coercion (actual_type, required_type, node, operator->name))
        return EXPR_ERROR;

      /* Parse the right-hand side and coerce to type
         REQUIRED_TYPE. */
      if (!type_coercion (parse_next_level (&rhs), required_type,
                          &rhs, operator->name))
        {
          free_node (*node);
          *node = NULL;
          return EXPR_ERROR;
        }
      *node = allocate_binary_nonterminal (operator->type, *node, rhs);

      /* The result is of type RESULT_TYPE. */
      actual_type = result_type;
    }

  if (op_count > 1 && chain_warning != NULL)
    msg (SW, chain_warning);

  return actual_type;
}

static enum expr_type
parse_inverting_unary_operator (union any_node **node,
                                enum expr_type required_type,
                                const struct operator *operator,
                                parse_recursively_func *parse_next_level) 
{
  unsigned op_count;

  op_count = 0;
  while (match_operator (operator, 1, NULL))
    op_count++;
  if (op_count == 0)
    return parse_next_level (node);

  if (!type_coercion (parse_next_level (node), required_type,
                      node, operator->name))
    return EXPR_ERROR;
  if (op_count % 2 != 0)
    *node = allocate_nonterminal (operator->type, *node);
  return required_type;
}

/* Parses the OR level. */
static enum expr_type
parse_or (union any_node **n)
{
  static const struct operator ops[] = 
    {
      { T_OR, OP_OR, "logical disjunction (\"OR\")" },
    };
  
  return parse_binary_operators (n, parse_and (n), EXPR_BOOLEAN, EXPR_BOOLEAN,
                                 ops, sizeof ops / sizeof *ops,
                                 parse_and, NULL);
}

/* Parses the AND level. */
static enum expr_type
parse_and (union any_node ** n)
{
  static const struct operator ops[] = 
    {
      { T_AND, OP_AND, "logical conjunction (\"AND\")" },
    };
  
  return parse_binary_operators (n, parse_not (n), EXPR_BOOLEAN, EXPR_BOOLEAN,
                                 ops, sizeof ops / sizeof *ops,
                                 parse_not, NULL);
}

/* Parses the NOT level. */
static enum expr_type
parse_not (union any_node ** n)
{
  static const struct operator op
    = { T_NOT, OP_NOT, "logical negation (\"NOT-\")" };
  return parse_inverting_unary_operator (n, EXPR_BOOLEAN, &op, parse_rel);
}

/* Parse relational operators. */
static enum expr_type
parse_rel (union any_node **n) 
{
  static const struct operator numeric_ops[] = 
    {
      { '=', OP_EQ, "numeric equality (\"=\")" },
      { T_EQ, OP_EQ, "numeric equality (\"EQ\")" },
      { T_GE, OP_GE, "numeric greater-than-or-equal-to (\">=\")" },
      { T_GT, OP_GT, "numeric greater than (\">\")" },
      { T_LE, OP_LE, "numeric less-than-or-equal-to (\"<=\")" },
      { T_LT, OP_LT, "numeric less than (\"<\")" },
      { T_NE, OP_NE, "numeric inequality (\"<>\")" },
    };

  static const struct operator string_ops[] = 
    {
      { '=', OP_EQ_STRING, "string equality (\"=\")" },
      { T_EQ, OP_EQ_STRING, "string equality (\"EQ\")" },
      { T_GE, OP_GE_STRING, "string greater-than-or-equal-to (\">=\")" },
      { T_GT, OP_GT_STRING, "string greater than (\">\")" },
      { T_LE, OP_LE_STRING, "string less-than-or-equal-to (\"<=\")" },
      { T_LT, OP_LT_STRING, "string less than (\"<\")" },
      { T_NE, OP_NE_STRING, "string inequality (\"<>\")" },
    };

  int type = parse_add (n);

  const char *chain_warning = 
    _("Chaining relational operators (e.g. \"a < b < c\") will "
      "not produce the mathematically expected result.  "
      "Use the AND logical operator to fix the problem "
      "(e.g. \"a < b AND b < c\").  "
      "If chaining is really intended, parentheses will disable "
      "this warning (e.g. \"(a < b) < c\".)");

  switch (type) 
    {
    case EXPR_ERROR:
      return EXPR_ERROR;

    case EXPR_NUMERIC:
    case EXPR_BOOLEAN:
      return parse_binary_operators (n,
                                     type, EXPR_NUMERIC, EXPR_BOOLEAN,
                                     numeric_ops,
                                     sizeof numeric_ops / sizeof *numeric_ops,
                                     parse_add, chain_warning);

    case EXPR_STRING:
      return parse_binary_operators (n,
                                     type, EXPR_STRING, EXPR_BOOLEAN,
                                     string_ops,
                                     sizeof string_ops / sizeof *string_ops,
                                     parse_add, chain_warning);

    default:
      assert (0);
      abort ();
    }
}

/* Parses the addition and subtraction level. */
static enum expr_type
parse_add (union any_node **n)
{
  static const struct operator ops[] = 
    {
      { '+', OP_ADD, "addition (\"+\")" },
      { '-', OP_SUB, "subtraction (\"-\")-" },
    };
  
  return parse_binary_operators (n, parse_mul (n), EXPR_NUMERIC, EXPR_NUMERIC,
                                 ops, sizeof ops / sizeof *ops,
                                 parse_mul, NULL);
}

/* Parses the multiplication and division level. */
static enum expr_type
parse_mul (union any_node ** n)
{
  static const struct operator ops[] = 
    {
      { '*', OP_MUL, "multiplication (\"*\")" },
      { '/', OP_DIV, "division (\"/\")" },
    };
  
  return parse_binary_operators (n, parse_neg (n), EXPR_NUMERIC, EXPR_NUMERIC,
                                 ops, sizeof ops / sizeof *ops,
                                 parse_neg, NULL);
}

/* Parses the unary minus level. */
static enum expr_type
parse_neg (union any_node **n)
{
  static const struct operator op = { '-', OP_NEG, "negation (\"-\")" };
  return parse_inverting_unary_operator (n, EXPR_NUMERIC, &op, parse_exp);
}

static enum expr_type
parse_exp (union any_node **n)
{
  static const struct operator ops[] = 
    {
      { T_EXP, OP_POW, "exponentiation (\"**\")" },
    };
  
  const char *chain_warning = 
    _("The exponentiation operator (\"**\") is left-associative, "
      "even though right-associative semantics are more useful.  "
      "That is, \"a**b**c\" equals \"(a**b)**c\", not as \"a**(b**c)\".  "
      "To disable this warning, insert parentheses.");

  return parse_binary_operators (n,
                                 parse_primary (n), EXPR_NUMERIC, EXPR_NUMERIC,
                                 ops, sizeof ops / sizeof *ops,
                                 parse_primary, chain_warning);
}

/* Parses system variables. */
static enum expr_type
parse_sysvar (union any_node **n)
{
  if (!strcmp (tokid, "$CASENUM"))
    {
      *n = xmalloc (sizeof (struct casenum_node));
      (*n)->casenum.type = OP_CASENUM;
      return EXPR_NUMERIC;
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
      return EXPR_STRING;
    }
  else
    {
      enum expr_type type;
      double d;

      type = EXPR_NUMERIC;
      if (!strcmp (tokid, "$TRUE")) 
        {
          d = 1.0;
          type = EXPR_BOOLEAN; 
        }
      else if (!strcmp (tokid, "$FALSE")) 
        {
          d = 0.0;
          type = EXPR_BOOLEAN; 
        }
      else if (!strcmp (tokid, "$SYSMIS"))
	d = SYSMIS;
      else if (!strcmp (tokid, "$JDATE"))
	{
	  struct tm *time = localtime (&last_vfm_invocation);
	  d = yrmoda (time->tm_year + 1900, time->tm_mon + 1, time->tm_mday);
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
        d = get_viewlength ();
      else if (!strcmp (tokid, "$WIDTH"))
        d = get_viewwidth ();
      else
	{
	  msg (SE, _("Unknown system variable %s."), tokid);
	  return EXPR_ERROR;
	}

      *n = xmalloc (sizeof (struct num_con_node));
      (*n)->num_con.type = OP_NUM_CON;
      (*n)->num_con.value = d;
      return type;
    }
}

/* Parses numbers, varnames, etc. */
static enum expr_type
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
	    enum expr_type type = parse_sysvar (n);
	    lex_get ();
	    return type;
	  }

	/* Otherwise, it must be a user variable. */
	v = dict_lookup_var (default_dict, tokid);
	lex_get ();
	if (v == NULL)
	  {
	    lex_error (_("expecting variable name"));
	    return EXPR_ERROR;
	  }

        if (v->type == NUMERIC) 
          {
            *n = allocate_var_node (OP_NUM_VAR, v);
            return EXPR_NUMERIC;
          }
        else 
          {
            *n = allocate_var_node (OP_STR_VAR, v);
            return EXPR_STRING; 
          }
      }

    case T_NUM:
      *n = allocate_num_con (tokval);
      lex_get ();
      return EXPR_NUMERIC;

    case T_STRING:
      {
        *n = allocate_str_con (ds_value (&tokstr), ds_length (&tokstr));
	lex_get ();
	return EXPR_STRING;
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
	    return EXPR_ERROR;
	  }
	return t;
      }

    default:
      lex_error (_("in expression"));
      return EXPR_ERROR;
    }
}

/* Individual function parsing. */

struct function
  {
    const char *s;
    int t;
    enum expr_type (*func) (const struct function *, int, union any_node **);
  };

static struct function func_tab[];
static int func_count;

static int get_num_args (const struct function *, int, union any_node **);

static enum expr_type
unary_func (const struct function *f, int x UNUSED, union any_node ** n)
{
  if (!get_num_args (f, 1, n))
    return EXPR_ERROR;
  return EXPR_NUMERIC;
}

static enum expr_type
binary_func (const struct function *f, int x UNUSED, union any_node ** n)
{
  if (!get_num_args (f, 2, n))
    return EXPR_ERROR;
  return EXPR_NUMERIC;
}

static enum expr_type
ternary_func (const struct function *f, int x UNUSED, union any_node **n)
{
  if (!get_num_args (f, 3, n))
    return EXPR_ERROR;
  return EXPR_NUMERIC;
}

static enum expr_type
MISSING_func (const struct function *f, int x UNUSED, union any_node **n)
{
  if (!get_num_args (f, 1, n))
    return EXPR_ERROR;
  return EXPR_BOOLEAN;
}

static enum expr_type
SYSMIS_func (const struct function *f, int x UNUSED, union any_node **n)
{
  if (!get_num_args (f, 1, n))
    return EXPR_ERROR;
  if ((*n)->nonterm.arg[0]->type == OP_NUM_VAR) 
    {
      struct variable *v = (*n)->nonterm.arg[0]->var.v;
      free_node (*n);
      *n = allocate_var_node (OP_NUM_SYS, v);
    }
  return EXPR_BOOLEAN;
}

static enum expr_type
VALUE_func (const struct function *f UNUSED, int x UNUSED, union any_node **n)
{
  struct variable *v = parse_variable ();

  if (!v)
    return EXPR_ERROR;
  if (v->type == NUMERIC)
    {
      *n = allocate_var_node (OP_NUM_VAL, v);
      return EXPR_NUMERIC;
    }
  else
    {
      *n = allocate_var_node (OP_STR_VAR, v);
      return EXPR_STRING;
    }
}

static enum expr_type
LAG_func (const struct function *f UNUSED, int x UNUSED, union any_node **n)
{
  struct variable *v = parse_variable ();
  int nlag = 1;

  if (!v)
    return EXPR_ERROR;
  if (lex_match (','))
    {
      if (!lex_integer_p () || lex_integer () <= 0 || lex_integer () > 1000)
	{
	  msg (SE, _("Argument 2 to LAG must be a small positive "
		     "integer constant."));
	  return EXPR_ERROR;
	}
      
      nlag = lex_integer ();
      lex_get ();
    }
  n_lag = max (nlag, n_lag);
  *n = xmalloc (sizeof (struct lag_node));
  (*n)->lag.type = (v->type == NUMERIC ? OP_NUM_LAG : OP_STR_LAG);
  (*n)->lag.v = v;
  (*n)->lag.lag = nlag;
  return (v->type == NUMERIC ? EXPR_NUMERIC : EXPR_STRING);
}

/* This screwball function parses n-ary operators:

   1. NMISS, NVALID, SUM, MEAN: any number of numeric
      arguments.

   2. SD, VARIANCE, CFVAR: at least two numeric arguments.

   3. RANGE: An odd number of arguments, but at least three, and
      all of the same type.

   4. ANY: At least two arguments, all of the same type.

   5. MIN, MAX: Any number of arguments, all of the same type.
 */
static enum expr_type
nary_num_func (const struct function *f, int min_args, union any_node **n)
{
  /* Argument number of current argument (used for error messages). */
  int arg_idx = 1;

  /* Number of arguments. */
  int nargs;

  /* Number of arguments allocated. */
  int m = 16;

  /* Type of arguments. */
  int type = (f->t == OP_ANY || f->t == OP_RANGE
              || f->t == OP_MIN || f->t == OP_MAX) ? -1 : NUMERIC;

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
			 arg_idx, f->s, var_type_name (type), var_type_name (v[j]->type));
		    free (v);
		    goto fail;
		  }
	    }
	  for (j = 0; j < nv; j++)
	    {
	      union any_node **c = &(*n)->nonterm.arg[(*n)->nonterm.n++];
              *c = allocate_var_node ((type == NUMERIC
                                       ? OP_NUM_VAR : OP_STR_VAR),
                                      v[j]);
	    }
	}
      else
	{
	  union any_node *c;
	  int t = parse_or (&c);

	  if (t == EXPR_ERROR)
	    goto fail;
	  if (t == EXPR_BOOLEAN)
	    {
	      free_node (c);
	      msg (SE, _("%s cannot take Boolean operands."), f->s);
	      goto fail;
	    }
	  if (type == -1)
	    {
	      if (t == EXPR_NUMERIC)
		type = NUMERIC;
	      else if (t == EXPR_STRING)
		type = ALPHA;
	    }
	  else if ((t == EXPR_NUMERIC) ^ (type == NUMERIC))
	    {
	      free_node (c);
	      msg (SE, _("Type mismatch in argument %d of %s, which was "
			 "expected to be of %s type.  It was actually "
			 "of %s type. "),
		   arg_idx, f->s, var_type_name (type), expr_type_name (t));
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

      arg_idx++;
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
          goto fail;
	}
    }
  else if (f->t == OP_SD || f->t == OP_VARIANCE
	   || f->t == OP_CFVAR || f->t == OP_ANY)
    {
      if (nargs < 2)
	{
	  msg (SE, _("%s requires at least two arguments."), f->s);
          goto fail;
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
      goto fail;
    }

  if (f->t == OP_MIN || f->t == OP_MAX) 
    {
      if (type == ALPHA) 
        {
          if (f->t == OP_MIN)
            (*n)->type = OP_MIN_STRING;
          else if (f->t == OP_MAX)
            (*n)->type = OP_MAX_STRING;
          else
            assert (0);
          return EXPR_STRING;
        }
      else
        return EXPR_NUMERIC;
    }
  else if (f->t == OP_ANY || f->t == OP_RANGE)
    {
      if (type == ALPHA) 
        {
          if (f->t == OP_ANY)
            (*n)->type = OP_ANY_STRING;
          else if (f->t == OP_RANGE)
            (*n)->type = OP_RANGE_STRING;
          else
            assert (0);
        }
      return EXPR_BOOLEAN;
    }
  else
    return EXPR_NUMERIC;

fail:
  free_node (*n);
  return EXPR_ERROR;
}

static enum expr_type
CONCAT_func (const struct function *f UNUSED, int x UNUSED, union any_node **n)
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
      if (type == EXPR_ERROR)
	goto fail;
      if (type != EXPR_STRING)
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
  return EXPR_STRING;

fail:
  free_node (*n);
  return EXPR_ERROR;
}

/* Parses a string function according to f->desc.  f->desc[0] is the
   return type of the function.  Succeeding characters represent
   successive args.  Optional args are separated from the required
   args by a slash (`/').  Codes are `n', numeric arg; `s', string
   arg; and `f', format spec (this must be the last arg).  If the
   optional args are included, the type becomes f->t+1. */
static enum expr_type
generic_str_func (const struct function *f, int x UNUSED, union any_node **n)
{
  struct string_function 
    {
      int t1, t2;
      enum expr_type return_type;
      const char *arg_types;
    };

  static const struct string_function string_func_tab[] = 
    {
      {OP_INDEX_2, OP_INDEX_3, EXPR_NUMERIC, "ssN"},
      {OP_RINDEX_2, OP_RINDEX_3, EXPR_NUMERIC, "ssN"},
      {OP_LENGTH, 0, EXPR_NUMERIC, "s"},
      {OP_LOWER, 0, EXPR_STRING, "s"},
      {OP_UPPER, 0, EXPR_STRING, "s"},
      {OP_LPAD, 0, EXPR_STRING, "snS"},
      {OP_RPAD, 0, EXPR_STRING, "snS"},
      {OP_LTRIM, 0, EXPR_STRING, "sS"},
      {OP_RTRIM, 0, EXPR_STRING, "sS"},
      {OP_NUMBER, 0, EXPR_NUMERIC, "sf"},
      {OP_STRING, 0, EXPR_STRING, "nf"},
      {OP_SUBSTR_2, OP_SUBSTR_3, EXPR_STRING, "snN"},
    };

  const int string_func_cnt = sizeof string_func_tab / sizeof *string_func_tab;

  const struct string_function *sf;
  int arg_cnt;
  const char *cp;
  struct nonterm_node *nonterm;

  /* Find string_function that corresponds to f. */
  for (sf = string_func_tab; sf < string_func_tab + string_func_cnt; sf++)
    if (f->t == sf->t1)
      break;
  assert (sf < string_func_tab + string_func_cnt);

  /* Count max number of arguments. */
  arg_cnt = 0;
  for (cp = sf->arg_types; *cp != '\0'; cp++)
    {
      if (*cp != 'f')
        arg_cnt++;
      else
	arg_cnt += 3;
    }

  /* Allocate node. */
  *n = xmalloc (sizeof (struct nonterm_node)
		+ (arg_cnt - 1) * sizeof (union any_node *));
  nonterm = &(*n)->nonterm;
  nonterm->type = sf->t1;
  nonterm->n = 0;

  /* Parse arguments. */
  cp = sf->arg_types;
  for (;;)
    {
      if (*cp == 'n' || *cp == 's' || *cp == 'N' || *cp == 'S')
	{
	  enum expr_type wanted_type
            = *cp == 'n' || *cp == 'N' ? EXPR_NUMERIC : EXPR_STRING;
	  enum expr_type actual_type = parse_or (&nonterm->arg[nonterm->n]);

	  if (actual_type == EXPR_ERROR)
	    goto fail;
          else if (actual_type == EXPR_BOOLEAN)
            actual_type = EXPR_NUMERIC;
	  if (actual_type != wanted_type)
	    {
	      msg (SE, _("Argument %d to %s was expected to be of %s type.  "
			 "It was actually of type %s."),
		   nonterm->n + 1, f->s,
		   expr_type_name (actual_type), expr_type_name (wanted_type));
	      goto fail;
	    }
	  nonterm->n++;
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
	  nonterm->arg[nonterm->n + 0] = (union any_node *) fmt.type;
	  nonterm->arg[nonterm->n + 1] = (union any_node *) fmt.w;
	  nonterm->arg[nonterm->n + 2] = (union any_node *) fmt.d;
	  break;
	}
      else
	assert (0);

      /* We're done if no args are left. */
      cp++;
      if (*cp == 0)
	break;

      /* Optional arguments are named with capital letters. */
      if (isupper ((unsigned char) *cp))
	{
          if (!lex_match (',')) 
            {
              if (sf->t2 == 0)
                {
                  if (*cp == 'N') 
                    nonterm->arg[nonterm->n++] = allocate_num_con (SYSMIS);
                  else if (*cp == 'S')
                    nonterm->arg[nonterm->n++] = allocate_str_con (" ", 1);
                  else
                    assert (0);
                }
              break; 
            }

          if (sf->t2 != 0)
            nonterm->type = sf->t2;
	}
      else if (!lex_match (','))
	{
	  msg (SE, _("Too few arguments to function %s."), f->s);
	  goto fail;
	}
    }

  return sf->return_type;

fail:
  free_node (*n);
  return EXPR_ERROR;
}

/* General function parsing. */

static int
get_num_args (const struct function *f, int num_args, union any_node **n)
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
      if (t == EXPR_ERROR)
	goto fail;
      (*n)->nonterm.n++;

      if (t == EXPR_STRING)
	{
	  msg (SE, _("Type mismatch in argument %d of %s.  A string "
                     "expression was supplied where only a numeric expression "
                     "is allowed."),
               i + 1, f->s);
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

static enum expr_type
parse_function (union any_node ** n)
{
  const struct function *fp;
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
      if (t == EXPR_ERROR)
	goto fail;
      if (t != EXPR_NUMERIC)
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

      return v->var[0]->type == NUMERIC ? EXPR_NUMERIC : EXPR_STRING;
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
    return EXPR_ERROR;
  
  {
    struct function f;
    f.s = fname;
    
    fp = binary_search (func_tab, func_count, sizeof *func_tab, &f,
                        compare_functions, NULL);
  }
  
  if (!fp)
    {
      msg (SE, _("There is no function named %s."), fname);
      return EXPR_ERROR;
    }
  if (min_args && fp->func != nary_num_func)
    {
      msg (SE, _("Function %s may not be given a minimum number of "
		 "arguments."), fname);
      return EXPR_ERROR;
    }
  t = fp->func (fp, min_args, n);
  if (t == EXPR_ERROR)
    return EXPR_ERROR;
  if (!lex_match (')'))
    {
      lex_error (_("expecting `)' after %s function"), fname);
      goto fail;
    }

  return t;

fail:
  free_node (*n);
  return EXPR_ERROR;
}

/* Utility functions. */

static const char *
expr_type_name (enum expr_type type)
{
  switch (type)
    {
    case EXPR_ERROR:
      return _("error");

    case EXPR_BOOLEAN:
      return _("Boolean");

    case EXPR_NUMERIC:
      return _("numeric");

    case EXPR_STRING:
      return _("string");

    default:
      assert (0);
      return 0;
    }
}

static const char *
var_type_name (int type)
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
  if (n != NULL) 
    {
      if (IS_NONTERMINAL (n->type))
        {
          int i;

          for (i = 0; i < n->nonterm.n; i++)
            free_node (n->nonterm.arg[i]);
        }
      free (n); 
    }
}

static union any_node *
allocate_num_con (double value) 
{
  union any_node *c;

  c = xmalloc (sizeof (struct num_con_node));
  c->num_con.type = OP_NUM_CON;
  c->num_con.value = value;

  return c;
}

static union any_node *
allocate_str_con (const char *string, size_t length) 
{
  union any_node *c;

  c = xmalloc (sizeof (struct str_con_node) + length - 1);
  c->str_con.type = OP_STR_CON;
  c->str_con.len = length;
  memcpy (c->str_con.s, string, length);

  return c;
}

static union any_node *
allocate_var_node (int type, struct variable *variable) 
{
  union any_node *c;

  c = xmalloc (sizeof (struct var_node));
  c->var.type = type;
  c->var.v = variable;

  return c;
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

static union any_node *
allocate_binary_nonterminal (int op, union any_node *lhs, union any_node *rhs) 
{
  union any_node *node;

  node = xmalloc (sizeof node->nonterm + sizeof *node->nonterm.arg);
  node->nonterm.type = op;
  node->nonterm.n = 2;
  node->nonterm.arg[0] = lhs;
  node->nonterm.arg[1] = rhs;

  return node;
}

static struct function func_tab[] =
{
  {"ABS", OP_ABS, unary_func},
  {"ACOS", OP_ARCOS, unary_func},
  {"ARCOS", OP_ARCOS, unary_func},
  {"ARSIN", OP_ARSIN, unary_func},
  {"ARTAN", OP_ARTAN, unary_func},
  {"ASIN", OP_ARSIN, unary_func},
  {"ATAN", OP_ARTAN, unary_func},
  {"COS", OP_COS, unary_func},
  {"EXP", OP_EXP, unary_func},
  {"LG10", OP_LG10, unary_func},
  {"LN", OP_LN, unary_func},
  {"MOD10", OP_MOD10, unary_func},
  {"NORMAL", OP_NORMAL, unary_func},
  {"RND", OP_RND, unary_func},
  {"SIN", OP_SIN, unary_func},
  {"SQRT", OP_SQRT, unary_func},
  {"TAN", OP_TAN, unary_func},
  {"TRUNC", OP_TRUNC, unary_func},
  {"UNIFORM", OP_UNIFORM, unary_func},

  {"TIME.DAYS", OP_TIME_DAYS, unary_func},
  {"TIME.HMS", OP_TIME_HMS, ternary_func},

  {"CTIME.DAYS", OP_CTIME_DAYS, unary_func},
  {"CTIME.HOURS", OP_CTIME_HOURS, unary_func},
  {"CTIME.MINUTES", OP_CTIME_MINUTES, unary_func},
  {"CTIME.SECONDS", OP_CTIME_SECONDS, unary_func},

  {"DATE.DMY", OP_DATE_DMY, ternary_func},
  {"DATE.MDY", OP_DATE_MDY, ternary_func},
  {"DATE.MOYR", OP_DATE_MOYR, binary_func},
  {"DATE.QYR", OP_DATE_QYR, binary_func},
  {"DATE.WKYR", OP_DATE_WKYR, binary_func},
  {"DATE.YRDAY", OP_DATE_YRDAY, binary_func},

  {"XDATE.DATE", OP_XDATE_DATE, unary_func},
  {"XDATE.HOUR", OP_XDATE_HOUR, unary_func},
  {"XDATE.JDAY", OP_XDATE_JDAY, unary_func},
  {"XDATE.MDAY", OP_XDATE_MDAY, unary_func},
  {"XDATE.MINUTE", OP_XDATE_MINUTE, unary_func},
  {"XDATE.MONTH", OP_XDATE_MONTH, unary_func},
  {"XDATE.QUARTER", OP_XDATE_QUARTER, unary_func},
  {"XDATE.SECOND", OP_XDATE_SECOND, unary_func},
  {"XDATE.TDAY", OP_XDATE_TDAY, unary_func},
  {"XDATE.TIME", OP_XDATE_TIME, unary_func},
  {"XDATE.WEEK", OP_XDATE_WEEK, unary_func},
  {"XDATE.WKDAY", OP_XDATE_WKDAY, unary_func},
  {"XDATE.YEAR", OP_XDATE_YEAR, unary_func},

  {"MISSING", OP_SYSMIS, MISSING_func},
  {"MOD", OP_MOD, binary_func},
  {"SYSMIS", OP_SYSMIS, SYSMIS_func},
  {"VALUE", OP_NUM_VAL, VALUE_func},
  {"LAG", OP_NUM_LAG, LAG_func},
  {"YRMODA", OP_YRMODA, ternary_func},

  {"ANY", OP_ANY, nary_num_func},
  {"CFVAR", OP_CFVAR, nary_num_func},
  {"MAX", OP_MAX, nary_num_func},
  {"MEAN", OP_MEAN, nary_num_func},
  {"MIN", OP_MIN, nary_num_func},
  {"NMISS", OP_NMISS, nary_num_func},
  {"NVALID", OP_NVALID, nary_num_func},
  {"RANGE", OP_RANGE, nary_num_func},
  {"SD", OP_SD, nary_num_func},
  {"SUM", OP_SUM, nary_num_func},
  {"VAR", OP_VARIANCE, nary_num_func},
  {"VARIANCE", OP_VARIANCE, nary_num_func},

  {"CONCAT", OP_CONCAT, CONCAT_func},

  {"INDEX", OP_INDEX_2, generic_str_func},
  {"RINDEX", OP_RINDEX_2, generic_str_func},
  {"LENGTH", OP_LENGTH, generic_str_func},
  {"LOWER", OP_LOWER, generic_str_func},
  {"UPCASE", OP_UPPER, generic_str_func},
  {"LPAD", OP_LPAD, generic_str_func},
  {"RPAD", OP_RPAD, generic_str_func},
  {"LTRIM", OP_LTRIM, generic_str_func},
  {"RTRIM", OP_RTRIM, generic_str_func},
  {"NUMBER", OP_NUMBER, generic_str_func},
  {"STRING", OP_STRING, generic_str_func},
  {"SUBSTR", OP_SUBSTR_2, generic_str_func},
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

void
expr_debug_print_postfix (const struct expression *e)
{
  const unsigned char *o;
  const double *num = e->num;
  const unsigned char *str = e->str;
  struct variable *const *v = e->var;
  int t;

  printf ("postfix:");
  for (o = e->op; *o != OP_SENTINEL;)
    {
      t = *o++;
      if (IS_NONTERMINAL (t))
	{
	  printf (" %s", ops[t].name);

	  if (ops[t].flags & OP_VAR_ARGS)
	    {
	      printf ("(%d)", *o);
	      o++;
	    }
	  if (ops[t].flags & OP_MIN_ARGS)
	    {
	      printf (".%d", *o);
	      o++;
	    }
	  if (ops[t].flags & OP_FMT_SPEC)
	    {
	      struct fmt_spec f;
	      f.type = (int) *o++;
	      f.w = (int) *o++;
	      f.d = (int) *o++;
	      printf ("(%s)", fmt_to_string (&f));
	    }
	}
      else if (t == OP_NUM_CON)
	{
	  if (*num == SYSMIS)
	    printf (" SYSMIS");
	  else
	    printf (" %f", *num);
	  num++;
	}
      else if (t == OP_STR_CON)
	{
	  printf (" \"%.*s\"", *str, &str[1]);
	  str += str[0] + 1;
	}
      else if (t == OP_NUM_VAR || t == OP_STR_VAR)
	{
	  printf (" %s", (*v)->name);
	  v++;
	}
      else if (t == OP_NUM_SYS)
	{
	  printf (" SYSMIS(#%d)", *o);
	  o++;
	}
      else if (t == OP_NUM_VAL)
	{
	  printf (" VALUE(#%d)", *o);
	  o++;
	}
      else if (t == OP_NUM_LAG || t == OP_STR_LAG)
	{
	  printf (" LAG(%s,%d)", (*v)->name, *o);
	  o++;
	  v++;
	}
      else
	{
	  printf ("%d unknown\n", t);
	  assert (0);
	}
    }
  putchar ('\n');
}

#define DEFINE_OPERATOR(NAME, STACK_DELTA, FLAGS, ARGS) \
        {#NAME, STACK_DELTA, FLAGS, ARGS},
struct op_desc ops[OP_SENTINEL] =
  {
#include "expr.def"
  };
