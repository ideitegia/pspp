/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2010, 2011, 2012, 2014 Free Software Foundation, Inc.

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

#include "private.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stdlib.h>

#include "data/case.h"
#include "data/dictionary.h"
#include "data/settings.h"
#include "data/variable.h"
#include "language/expressions/helpers.h"
#include "language/lexer/format-parser.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/c-strcase.h"
#include "gl/xalloc.h"

/* Declarations. */

/* Recursive descent parser in order of increasing precedence. */
typedef union any_node *parse_recursively_func (struct lexer *, struct expression *);
static parse_recursively_func parse_or, parse_and, parse_not;
static parse_recursively_func parse_rel, parse_add, parse_mul;
static parse_recursively_func parse_neg, parse_exp;
static parse_recursively_func parse_primary;
static parse_recursively_func parse_vector_element, parse_function;

/* Utility functions. */
static struct expression *expr_create (struct dataset *ds);
atom_type expr_node_returns (const union any_node *);

static const char *atom_type_name (atom_type);
static struct expression *finish_expression (union any_node *,
                                             struct expression *);
static bool type_check (struct expression *, union any_node **,
                        enum expr_type expected_type);
static union any_node *allocate_unary_variable (struct expression *,
                                                const struct variable *);

/* Public functions. */

/* Parses an expression of the given TYPE.
   If DICT is nonnull then variables and vectors within it may be
   referenced within the expression; otherwise, the expression
   must not reference any variables or vectors.
   Returns the new expression if successful or a null pointer
   otherwise. */
struct expression *
expr_parse (struct lexer *lexer, struct dataset *ds, enum expr_type type)
{
  union any_node *n;
  struct expression *e;

  assert (type == EXPR_NUMBER || type == EXPR_STRING || type == EXPR_BOOLEAN);

  e = expr_create (ds);
  n = parse_or (lexer, e);
  if (n != NULL && type_check (e, &n, type))
    return finish_expression (expr_optimize (n, e), e);
  else
    {
      expr_free (e);
      return NULL;
    }
}

/* Parses and returns an expression of the given TYPE, as
   expr_parse(), and sets up so that destroying POOL will free
   the expression as well. */
struct expression *
expr_parse_pool (struct lexer *lexer,
		 struct pool *pool,
		 struct dataset *ds,
                 enum expr_type type)
{
  struct expression *e = expr_parse (lexer, ds, type);
  if (e != NULL)
    pool_add_subpool (pool, e->expr_pool);
  return e;
}

/* Free expression E. */
void
expr_free (struct expression *e)
{
  if (e != NULL)
    pool_destroy (e->expr_pool);
}

struct expression *
expr_parse_any (struct lexer *lexer, struct dataset *ds, bool optimize)
{
  union any_node *n;
  struct expression *e;

  e = expr_create (ds);
  n = parse_or (lexer, e);
  if (n == NULL)
    {
      expr_free (e);
      return NULL;
    }

  if (optimize)
    n = expr_optimize (n, e);
  return finish_expression (n, e);
}

/* Finishing up expression building. */

/* Height of an expression's stacks. */
struct stack_heights
  {
    int number_height;  /* Height of number stack. */
    int string_height;  /* Height of string stack. */
  };

/* Stack heights used by different kinds of arguments. */
static const struct stack_heights on_number_stack = {1, 0};
static const struct stack_heights on_string_stack = {0, 1};
static const struct stack_heights not_on_stack = {0, 0};

/* Returns the stack heights used by an atom of the given
   TYPE. */
static const struct stack_heights *
atom_type_stack (atom_type type)
{
  assert (is_atom (type));

  switch (type)
    {
    case OP_number:
    case OP_boolean:
      return &on_number_stack;

    case OP_string:
      return &on_string_stack;

    case OP_format:
    case OP_ni_format:
    case OP_no_format:
    case OP_num_var:
    case OP_str_var:
    case OP_integer:
    case OP_pos_int:
    case OP_vector:
      return &not_on_stack;

    default:
      NOT_REACHED ();
    }
}

/* Measures the stack height needed for node N, supposing that
   the stack height is initially *HEIGHT and updating *HEIGHT to
   the final stack height.  Updates *MAX, if necessary, to
   reflect the maximum intermediate or final height. */
static void
measure_stack (const union any_node *n,
               struct stack_heights *height, struct stack_heights *max)
{
  const struct stack_heights *return_height;

  if (is_composite (n->type))
    {
      struct stack_heights args;
      int i;

      args = *height;
      for (i = 0; i < n->composite.arg_cnt; i++)
        measure_stack (n->composite.args[i], &args, max);

      return_height = atom_type_stack (operations[n->type].returns);
    }
  else
    return_height = atom_type_stack (n->type);

  height->number_height += return_height->number_height;
  height->string_height += return_height->string_height;

  if (height->number_height > max->number_height)
    max->number_height = height->number_height;
  if (height->string_height > max->string_height)
    max->string_height = height->string_height;
}

/* Allocates stacks within E sufficient for evaluating node N. */
static void
allocate_stacks (union any_node *n, struct expression *e)
{
  struct stack_heights initial = {0, 0};
  struct stack_heights max = {0, 0};

  measure_stack (n, &initial, &max);
  e->number_stack = pool_alloc (e->expr_pool,
                                sizeof *e->number_stack * max.number_height);
  e->string_stack = pool_alloc (e->expr_pool,
                                sizeof *e->string_stack * max.string_height);
}

/* Finalizes expression E for evaluating node N. */
static struct expression *
finish_expression (union any_node *n, struct expression *e)
{
  /* Allocate stacks. */
  allocate_stacks (n, e);

  /* Output postfix representation. */
  expr_flatten (n, e);

  /* The eval_pool might have been used for allocating strings
     during optimization.  We need to keep those strings around
     for all subsequent evaluations, so start a new eval_pool. */
  e->eval_pool = pool_create_subpool (e->expr_pool);

  return e;
}

/* Verifies that expression E, whose root node is *N, can be
   converted to type EXPECTED_TYPE, inserting a conversion at *N
   if necessary.  Returns true if successful, false on failure. */
static bool
type_check (struct expression *e,
            union any_node **n, enum expr_type expected_type)
{
  atom_type actual_type = expr_node_returns (*n);

  switch (expected_type)
    {
    case EXPR_BOOLEAN:
    case EXPR_NUMBER:
      if (actual_type != OP_number && actual_type != OP_boolean)
	{
	  msg (SE, _("Type mismatch: expression has %s type, "
                     "but a numeric value is required here."),
               atom_type_name (actual_type));
	  return false;
	}
      if (actual_type == OP_number && expected_type == EXPR_BOOLEAN)
        *n = expr_allocate_binary (e, OP_NUM_TO_BOOLEAN, *n,
                                   expr_allocate_string (e, ss_empty ()));
      break;

    case EXPR_STRING:
      if (actual_type != OP_string)
        {
          msg (SE, _("Type mismatch: expression has %s type, "
                     "but a string value is required here."),
               atom_type_name (actual_type));
          return false;
        }
      break;

    default:
      NOT_REACHED ();
    }

  return true;
}

/* Recursive-descent expression parser. */

/* Considers whether *NODE may be coerced to type REQUIRED_TYPE.
   Returns true if possible, false if disallowed.

   If DO_COERCION is false, then *NODE is not modified and there
   are no side effects.

   If DO_COERCION is true, we perform the coercion if possible,
   modifying *NODE if necessary.  If the coercion is not possible
   then we free *NODE and set *NODE to a null pointer.

   This function's interface is somewhat awkward.  Use one of the
   wrapper functions type_coercion(), type_coercion_assert(), or
   is_coercible() instead. */
static bool
type_coercion_core (struct expression *e,
                    atom_type required_type,
                    union any_node **node,
                    const char *operator_name,
                    bool do_coercion)
{
  atom_type actual_type;

  assert (!!do_coercion == (e != NULL));
  if (*node == NULL)
    {
      /* Propagate error.  Whatever caused the original error
         already emitted an error message. */
      return false;
    }

  actual_type = expr_node_returns (*node);
  if (actual_type == required_type)
    {
      /* Type match. */
      return true;
    }

  switch (required_type)
    {
    case OP_number:
      if (actual_type == OP_boolean)
        {
          /* To enforce strict typing rules, insert Boolean to
             numeric "conversion".  This conversion is a no-op,
             so it will be removed later. */
          if (do_coercion)
            *node = expr_allocate_unary (e, OP_BOOLEAN_TO_NUM, *node);
          return true;
        }
      break;

    case OP_string:
      /* No coercion to string. */
      break;

    case OP_boolean:
      if (actual_type == OP_number)
        {
          /* Convert numeric to boolean. */
          if (do_coercion)
            {
              union any_node *op_name;

              op_name = expr_allocate_string (e, ss_cstr (operator_name));
              *node = expr_allocate_binary (e, OP_NUM_TO_BOOLEAN, *node,
                                            op_name);
            }
          return true;
        }
      break;

    case OP_format:
      NOT_REACHED ();

    case OP_ni_format:
      msg_disable ();
      if ((*node)->type == OP_format
          && fmt_check_input (&(*node)->format.f)
          && fmt_check_type_compat (&(*node)->format.f, VAL_NUMERIC))
        {
          msg_enable ();
          if (do_coercion)
            (*node)->type = OP_ni_format;
          return true;
        }
      msg_enable ();
      break;

    case OP_no_format:
      msg_disable ();
      if ((*node)->type == OP_format
          && fmt_check_output (&(*node)->format.f)
          && fmt_check_type_compat (&(*node)->format.f, VAL_NUMERIC))
        {
          msg_enable ();
          if (do_coercion)
            (*node)->type = OP_no_format;
          return true;
        }
      msg_enable ();
      break;

    case OP_num_var:
      if ((*node)->type == OP_NUM_VAR)
        {
          if (do_coercion)
            *node = (*node)->composite.args[0];
          return true;
        }
      break;

    case OP_str_var:
      if ((*node)->type == OP_STR_VAR)
        {
          if (do_coercion)
            *node = (*node)->composite.args[0];
          return true;
        }
      break;

    case OP_var:
      if ((*node)->type == OP_NUM_VAR || (*node)->type == OP_STR_VAR)
        {
          if (do_coercion)
            *node = (*node)->composite.args[0];
          return true;
        }
      break;

    case OP_pos_int:
      if ((*node)->type == OP_number
          && floor ((*node)->number.n) == (*node)->number.n
          && (*node)->number.n > 0 && (*node)->number.n < INT_MAX)
        {
          if (do_coercion)
            *node = expr_allocate_pos_int (e, (*node)->number.n);
          return true;
        }
      break;

    default:
      NOT_REACHED ();
    }

  if (do_coercion)
    {
      msg (SE, _("Type mismatch while applying %s operator: "
                 "cannot convert %s to %s."),
           operator_name,
           atom_type_name (actual_type), atom_type_name (required_type));
      *node = NULL;
    }
  return false;
}

/* Coerces *NODE to type REQUIRED_TYPE, and returns success.  If
   *NODE cannot be coerced to the desired type then we issue an
   error message about operator OPERATOR_NAME and free *NODE. */
static bool
type_coercion (struct expression *e,
               atom_type required_type, union any_node **node,
               const char *operator_name)
{
  return type_coercion_core (e, required_type, node, operator_name, true);
}

/* Coerces *NODE to type REQUIRED_TYPE.
   Assert-fails if the coercion is disallowed. */
static void
type_coercion_assert (struct expression *e,
                      atom_type required_type, union any_node **node)
{
  int success = type_coercion_core (e, required_type, node, NULL, true);
  assert (success);
}

/* Returns true if *NODE may be coerced to type REQUIRED_TYPE,
   false otherwise. */
static bool
is_coercible (atom_type required_type, union any_node *const *node)
{
  return type_coercion_core (NULL, required_type,
                             (union any_node **) node, NULL, false);
}

/* Returns true if ACTUAL_TYPE is a kind of REQUIRED_TYPE, false
   otherwise. */
static bool
is_compatible (atom_type required_type, atom_type actual_type)
{
  return (required_type == actual_type
          || (required_type == OP_var
              && (actual_type == OP_num_var || actual_type == OP_str_var)));
}

/* How to parse an operator. */
struct operator
  {
    int token;                  /* Token representing operator. */
    operation_type type;        /* Operation type representing operation. */
    const char *name;           /* Name of operator. */
  };

/* Attempts to match the current token against the tokens for the
   OP_CNT operators in OPS[].  If successful, returns true
   and, if OPERATOR is non-null, sets *OPERATOR to the operator.
   On failure, returns false and, if OPERATOR is non-null, sets
   *OPERATOR to a null pointer. */
static bool
match_operator (struct lexer *lexer, const struct operator ops[], size_t op_cnt,
                const struct operator **operator)
{
  const struct operator *op;

  for (op = ops; op < ops + op_cnt; op++)
    if (lex_token (lexer) == op->token)
      {
        if (op->token != T_NEG_NUM)
          lex_get (lexer);
        if (operator != NULL)
          *operator = op;
        return true;
      }
  if (operator != NULL)
    *operator = NULL;
  return false;
}

static bool
check_operator (const struct operator *op, int arg_cnt, atom_type arg_type)
{
  const struct operation *o;
  size_t i;

  assert (op != NULL);
  o = &operations[op->type];
  assert (o->arg_cnt == arg_cnt);
  assert ((o->flags & OPF_ARRAY_OPERAND) == 0);
  for (i = 0; i < arg_cnt; i++)
    assert (is_compatible (arg_type, o->args[i]));
  return true;
}

static bool
check_binary_operators (const struct operator ops[], size_t op_cnt,
                        atom_type arg_type)
{
  size_t i;

  for (i = 0; i < op_cnt; i++)
    check_operator (&ops[i], 2, arg_type);
  return true;
}

static atom_type
get_operand_type (const struct operator *op)
{
  return operations[op->type].args[0];
}

/* Parses a chain of left-associative operator/operand pairs.
   There are OP_CNT operators, specified in OPS[].  The
   operators' operands must all be the same type.  The next
   higher level is parsed by PARSE_NEXT_LEVEL.  If CHAIN_WARNING
   is non-null, then it will be issued as a warning if more than
   one operator/operand pair is parsed. */
static union any_node *
parse_binary_operators (struct lexer *lexer, struct expression *e, union any_node *node,
                        const struct operator ops[], size_t op_cnt,
                        parse_recursively_func *parse_next_level,
                        const char *chain_warning)
{
  atom_type operand_type = get_operand_type (&ops[0]);
  int op_count;
  const struct operator *operator;

  assert (check_binary_operators (ops, op_cnt, operand_type));
  if (node == NULL)
    return node;

  for (op_count = 0; match_operator (lexer, ops, op_cnt, &operator); op_count++)
    {
      union any_node *rhs;

      /* Convert the left-hand side to type OPERAND_TYPE. */
      if (!type_coercion (e, operand_type, &node, operator->name))
        return NULL;

      /* Parse the right-hand side and coerce to type
         OPERAND_TYPE. */
      rhs = parse_next_level (lexer, e);
      if (!type_coercion (e, operand_type, &rhs, operator->name))
        return NULL;
      node = expr_allocate_binary (e, operator->type, node, rhs);
    }

  if (op_count > 1 && chain_warning != NULL)
    msg (SW, "%s", chain_warning);

  return node;
}

static union any_node *
parse_inverting_unary_operator (struct lexer *lexer, struct expression *e,
                                const struct operator *op,
                                parse_recursively_func *parse_next_level)
{
  union any_node *node;
  unsigned op_count;

  check_operator (op, 1, get_operand_type (op));

  op_count = 0;
  while (match_operator (lexer, op, 1, NULL))
    op_count++;

  node = parse_next_level (lexer, e);
  if (op_count > 0
      && type_coercion (e, get_operand_type (op), &node, op->name)
      && op_count % 2 != 0)
    return expr_allocate_unary (e, op->type, node);
  else
    return node;
}

/* Parses the OR level. */
static union any_node *
parse_or (struct lexer *lexer, struct expression *e)
{
  static const struct operator op =
    { T_OR, OP_OR, "logical disjunction (`OR')" };

  return parse_binary_operators (lexer, e, parse_and (lexer, e), &op, 1, parse_and, NULL);
}

/* Parses the AND level. */
static union any_node *
parse_and (struct lexer *lexer, struct expression *e)
{
  static const struct operator op =
    { T_AND, OP_AND, "logical conjunction (`AND')" };

  return parse_binary_operators (lexer, e, parse_not (lexer, e),
				 &op, 1, parse_not, NULL);
}

/* Parses the NOT level. */
static union any_node *
parse_not (struct lexer *lexer, struct expression *e)
{
  static const struct operator op
    = { T_NOT, OP_NOT, "logical negation (`NOT')" };
  return parse_inverting_unary_operator (lexer, e, &op, parse_rel);
}

/* Parse relational operators. */
static union any_node *
parse_rel (struct lexer *lexer, struct expression *e)
{
  const char *chain_warning =
    _("Chaining relational operators (e.g. `a < b < c') will "
      "not produce the mathematically expected result.  "
      "Use the AND logical operator to fix the problem "
      "(e.g. `a < b AND b < c').  "
      "If chaining is really intended, parentheses will disable "
      "this warning (e.g. `(a < b) < c'.)");

  union any_node *node = parse_add (lexer, e);

  if (node == NULL)
    return NULL;

  switch (expr_node_returns (node))
    {
    case OP_number:
    case OP_boolean:
      {
        static const struct operator ops[] =
          {
            { T_EQUALS, OP_EQ, "numeric equality (`=')" },
            { T_EQ, OP_EQ, "numeric equality (`EQ')" },
            { T_GE, OP_GE, "numeric greater-than-or-equal-to (`>=')" },
            { T_GT, OP_GT, "numeric greater than (`>')" },
            { T_LE, OP_LE, "numeric less-than-or-equal-to (`<=')" },
            { T_LT, OP_LT, "numeric less than (`<')" },
            { T_NE, OP_NE, "numeric inequality (`<>')" },
          };

        return parse_binary_operators (lexer, e, node, ops,
				       sizeof ops / sizeof *ops,
                                       parse_add, chain_warning);
      }

    case OP_string:
      {
        static const struct operator ops[] =
          {
            { T_EQUALS, OP_EQ_STRING, "string equality (`=')" },
            { T_EQ, OP_EQ_STRING, "string equality (`EQ')" },
            { T_GE, OP_GE_STRING, "string greater-than-or-equal-to (`>=')" },
            { T_GT, OP_GT_STRING, "string greater than (`>')" },
            { T_LE, OP_LE_STRING, "string less-than-or-equal-to (`<=')" },
            { T_LT, OP_LT_STRING, "string less than (`<')" },
            { T_NE, OP_NE_STRING, "string inequality (`<>')" },
          };

        return parse_binary_operators (lexer, e, node, ops,
				       sizeof ops / sizeof *ops,
                                       parse_add, chain_warning);
      }

    default:
      return node;
    }
}

/* Parses the addition and subtraction level. */
static union any_node *
parse_add (struct lexer *lexer, struct expression *e)
{
  static const struct operator ops[] =
    {
      { T_PLUS, OP_ADD, "addition (`+')" },
      { T_DASH, OP_SUB, "subtraction (`-')" },
      { T_NEG_NUM, OP_ADD, "subtraction (`-')" },
    };

  return parse_binary_operators (lexer, e, parse_mul (lexer, e),
                                 ops, sizeof ops / sizeof *ops,
                                 parse_mul, NULL);
}

/* Parses the multiplication and division level. */
static union any_node *
parse_mul (struct lexer *lexer, struct expression *e)
{
  static const struct operator ops[] =
    {
      { T_ASTERISK, OP_MUL, "multiplication (`*')" },
      { T_SLASH, OP_DIV, "division (`/')" },
    };

  return parse_binary_operators (lexer, e, parse_neg (lexer, e),
                                 ops, sizeof ops / sizeof *ops,
                                 parse_neg, NULL);
}

/* Parses the unary minus level. */
static union any_node *
parse_neg (struct lexer *lexer, struct expression *e)
{
  static const struct operator op = { T_DASH, OP_NEG, "negation (`-')" };
  return parse_inverting_unary_operator (lexer, e, &op, parse_exp);
}

static union any_node *
parse_exp (struct lexer *lexer, struct expression *e)
{
  static const struct operator op =
    { T_EXP, OP_POW, "exponentiation (`**')" };

  const char *chain_warning =
    _("The exponentiation operator (`**') is left-associative, "
      "even though right-associative semantics are more useful.  "
      "That is, `a**b**c' equals `(a**b)**c', not as `a**(b**c)'.  "
      "To disable this warning, insert parentheses.");

  union any_node *lhs, *node;
  bool negative = false;

  if (lex_token (lexer) == T_NEG_NUM)
    {
      lhs = expr_allocate_number (e, -lex_tokval (lexer));
      negative = true;
      lex_get (lexer);
    }
  else
    lhs = parse_primary (lexer, e);

  node = parse_binary_operators (lexer, e, lhs, &op, 1,
                                  parse_primary, chain_warning);
  return negative ? expr_allocate_unary (e, OP_NEG, node) : node;
}

/* Parses system variables. */
static union any_node *
parse_sysvar (struct lexer *lexer, struct expression *e)
{
  if (lex_match_id (lexer, "$CASENUM"))
    return expr_allocate_nullary (e, OP_CASENUM);
  else if (lex_match_id (lexer, "$DATE"))
    {
      static const char *months[12] =
        {
          "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
          "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
        };

      time_t last_proc_time = time_of_last_procedure (e->ds);
      struct tm *time;
      char temp_buf[10];
      struct substring s;

      time = localtime (&last_proc_time);
      sprintf (temp_buf, "%02d %s %02d", abs (time->tm_mday) % 100,
               months[abs (time->tm_mon) % 12], abs (time->tm_year) % 100);

      ss_alloc_substring (&s, ss_cstr (temp_buf));
      return expr_allocate_string (e, s);
    }
  else if (lex_match_id (lexer, "$TRUE"))
    return expr_allocate_boolean (e, 1.0);
  else if (lex_match_id (lexer, "$FALSE"))
    return expr_allocate_boolean (e, 0.0);
  else if (lex_match_id (lexer, "$SYSMIS"))
    return expr_allocate_number (e, SYSMIS);
  else if (lex_match_id (lexer, "$JDATE"))
    {
      time_t time = time_of_last_procedure (e->ds);
      struct tm *tm = localtime (&time);
      return expr_allocate_number (e, expr_ymd_to_ofs (tm->tm_year + 1900,
                                                       tm->tm_mon + 1,
                                                       tm->tm_mday));
    }
  else if (lex_match_id (lexer, "$TIME"))
    {
      time_t time = time_of_last_procedure (e->ds);
      struct tm *tm = localtime (&time);
      return expr_allocate_number (e,
                                   expr_ymd_to_date (tm->tm_year + 1900,
                                                     tm->tm_mon + 1,
                                                     tm->tm_mday)
                                   + tm->tm_hour * 60 * 60.
                                   + tm->tm_min * 60.
                                   + tm->tm_sec);
    }
  else if (lex_match_id (lexer, "$LENGTH"))
    return expr_allocate_number (e, settings_get_viewlength ());
  else if (lex_match_id (lexer, "$WIDTH"))
    return expr_allocate_number (e, settings_get_viewwidth ());
  else
    {
      msg (SE, _("Unknown system variable %s."), lex_tokcstr (lexer));
      return NULL;
    }
}

/* Parses numbers, varnames, etc. */
static union any_node *
parse_primary (struct lexer *lexer, struct expression *e)
{
  switch (lex_token (lexer))
    {
    case T_ID:
      if (lex_next_token (lexer, 1) == T_LPAREN)
        {
          /* An identifier followed by a left parenthesis may be
             a vector element reference.  If not, it's a function
             call. */
          if (e->ds != NULL && dict_lookup_vector (dataset_dict (e->ds), lex_tokcstr (lexer)) != NULL)
            return parse_vector_element (lexer, e);
          else
            return parse_function (lexer, e);
        }
      else if (lex_tokcstr (lexer)[0] == '$')
        {
          /* $ at the beginning indicates a system variable. */
          return parse_sysvar (lexer, e);
        }
      else if (e->ds != NULL && dict_lookup_var (dataset_dict (e->ds), lex_tokcstr (lexer)))
        {
          /* It looks like a user variable.
             (It could be a format specifier, but we'll assume
             it's a variable unless proven otherwise. */
          return allocate_unary_variable (e, parse_variable (lexer, dataset_dict (e->ds)));
        }
      else
        {
          /* Try to parse it as a format specifier. */
          struct fmt_spec fmt;
          bool ok;

          msg_disable ();
          ok = parse_format_specifier (lexer, &fmt);
          msg_enable ();

          if (ok)
            return expr_allocate_format (e, &fmt);

          /* All attempts failed. */
          msg (SE, _("Unknown identifier %s."), lex_tokcstr (lexer));
          return NULL;
        }
      break;

    case T_POS_NUM:
    case T_NEG_NUM:
      {
        union any_node *node = expr_allocate_number (e, lex_tokval (lexer) );
        lex_get (lexer);
        return node;
      }

    case T_STRING:
      {
        const char *dict_encoding;
        union any_node *node;
        char *s;

        dict_encoding = (e->ds != NULL
                         ? dict_get_encoding (dataset_dict (e->ds))
                         : "UTF-8");
        s = recode_string_pool (dict_encoding, "UTF-8", lex_tokcstr (lexer),
                           ss_length (lex_tokss (lexer)), e->expr_pool);
        node = expr_allocate_string (e, ss_cstr (s));

	lex_get (lexer);
	return node;
      }

    case T_LPAREN:
      {
        union any_node *node;
	lex_get (lexer);
	node = parse_or (lexer, e);
	if (node != NULL && !lex_force_match (lexer, T_RPAREN))
          return NULL;
        return node;
      }

    default:
      lex_error (lexer, NULL);
      return NULL;
    }
}

static union any_node *
parse_vector_element (struct lexer *lexer, struct expression *e)
{
  const struct vector *vector;
  union any_node *element;

  /* Find vector, skip token.
     The caller must already have verified that the current token
     is the name of a vector. */
  vector = dict_lookup_vector (dataset_dict (e->ds), lex_tokcstr (lexer));
  assert (vector != NULL);
  lex_get (lexer);

  /* Skip left parenthesis token.
     The caller must have verified that the lookahead is a left
     parenthesis. */
  assert (lex_token (lexer) == T_LPAREN);
  lex_get (lexer);

  element = parse_or (lexer, e);
  if (!type_coercion (e, OP_number, &element, "vector indexing")
      || !lex_match (lexer, T_RPAREN))
    return NULL;

  return expr_allocate_binary (e, (vector_get_type (vector) == VAL_NUMERIC
                                   ? OP_VEC_ELEM_NUM : OP_VEC_ELEM_STR),
                               element, expr_allocate_vector (e, vector));
}

/* Individual function parsing. */

const struct operation operations[OP_first + OP_cnt] = {
#include "parse.inc"
};

static bool
word_matches (const char **test, const char **name)
{
  size_t test_len = strcspn (*test, ".");
  size_t name_len = strcspn (*name, ".");
  if (test_len == name_len)
    {
      if (buf_compare_case (*test, *name, test_len))
        return false;
    }
  else if (test_len < 3 || test_len > name_len)
    return false;
  else
    {
      if (buf_compare_case (*test, *name, test_len))
        return false;
    }

  *test += test_len;
  *name += name_len;
  if (**test != **name)
    return false;

  if (**test == '.')
    {
      (*test)++;
      (*name)++;
    }
  return true;
}

static int
compare_names (const char *test, const char *name, bool abbrev_ok)
{
  if (!abbrev_ok)
    return true;

  for (;;)
    {
      if (!word_matches (&test, &name))
        return true;
      if (*name == '\0' && *test == '\0')
        return false;
    }
}

static int
compare_strings (const char *test, const char *name, bool abbrev_ok UNUSED)
{
  return c_strcasecmp (test, name);
}

static bool
lookup_function_helper (const char *name,
                        int (*compare) (const char *test, const char *name,
                                        bool abbrev_ok),
                        const struct operation **first,
                        const struct operation **last)
{
  const struct operation *f;

  for (f = operations + OP_function_first;
       f <= operations + OP_function_last; f++)
    if (!compare (name, f->name, !(f->flags & OPF_NO_ABBREV)))
      {
        *first = f;

        while (f <= operations + OP_function_last
               && !compare (name, f->name, !(f->flags & OPF_NO_ABBREV)))
          f++;
        *last = f;

        return true;
      }

  return false;
}

static bool
lookup_function (const char *name,
                 const struct operation **first,
                 const struct operation **last)
{
  *first = *last = NULL;
  return (lookup_function_helper (name, compare_strings, first, last)
          || lookup_function_helper (name, compare_names, first, last));
}

static int
extract_min_valid (const char *s)
{
  char *p = strrchr (s, '.');
  if (p == NULL
      || p[1] < '0' || p[1] > '9'
      || strspn (p + 1, "0123456789") != strlen (p + 1))
    return -1;
  *p = '\0';
  return atoi (p + 1);
}

static atom_type
function_arg_type (const struct operation *f, size_t arg_idx)
{
  assert (arg_idx < f->arg_cnt || (f->flags & OPF_ARRAY_OPERAND));

  return f->args[arg_idx < f->arg_cnt ? arg_idx : f->arg_cnt - 1];
}

static bool
match_function (union any_node **args, int arg_cnt, const struct operation *f)
{
  size_t i;

  if (arg_cnt < f->arg_cnt
      || (arg_cnt > f->arg_cnt && (f->flags & OPF_ARRAY_OPERAND) == 0)
      || arg_cnt - (f->arg_cnt - 1) < f->array_min_elems)
    return false;

  for (i = 0; i < arg_cnt; i++)
    if (!is_coercible (function_arg_type (f, i), &args[i]))
      return false;

  return true;
}

static void
coerce_function_args (struct expression *e, const struct operation *f,
                      union any_node **args, size_t arg_cnt)
{
  int i;

  for (i = 0; i < arg_cnt; i++)
    type_coercion_assert (e, function_arg_type (f, i), &args[i]);
}

static bool
validate_function_args (const struct operation *f, int arg_cnt, int min_valid)
{
  int array_arg_cnt = arg_cnt - (f->arg_cnt - 1);
  if (array_arg_cnt < f->array_min_elems)
    {
      msg (SE, _("%s must have at least %d arguments in list."),
           f->prototype, f->array_min_elems);
      return false;
    }

  if ((f->flags & OPF_ARRAY_OPERAND)
      && array_arg_cnt % f->array_granularity != 0)
    {
      if (f->array_granularity == 2)
        msg (SE, _("%s must have an even number of arguments in list."),
             f->prototype);
      else
        msg (SE, _("%s must have multiple of %d arguments in list."),
             f->prototype, f->array_granularity);
      return false;
    }

  if (min_valid != -1)
    {
      if (f->array_min_elems == 0)
        {
          assert ((f->flags & OPF_MIN_VALID) == 0);
          msg (SE, _("%s function does not accept a minimum valid "
                     "argument count."), f->prototype);
          return false;
        }
      else
        {
          assert (f->flags & OPF_MIN_VALID);
          if (array_arg_cnt < f->array_min_elems)
            {
              msg (SE, _("%s requires at least %d valid arguments in list."),
                   f->prototype, f->array_min_elems);
              return false;
            }
          else if (min_valid > array_arg_cnt)
            {
              msg (SE, _("With %s, "
                         "using minimum valid argument count of %d "
                         "does not make sense when passing only %d "
                         "arguments in list."),
                   f->prototype, min_valid, array_arg_cnt);
              return false;
            }
        }
    }

  return true;
}

static void
add_arg (union any_node ***args, int *arg_cnt, int *arg_cap,
         union any_node *arg)
{
  if (*arg_cnt >= *arg_cap)
    {
      *arg_cap += 8;
      *args = xrealloc (*args, sizeof **args * *arg_cap);
    }

  (*args)[(*arg_cnt)++] = arg;
}

static void
put_invocation (struct string *s,
                const char *func_name, union any_node **args, size_t arg_cnt)
{
  size_t i;

  ds_put_format (s, "%s(", func_name);
  for (i = 0; i < arg_cnt; i++)
    {
      if (i > 0)
        ds_put_cstr (s, ", ");
      ds_put_cstr (s, operations[expr_node_returns (args[i])].prototype);
    }
  ds_put_byte (s, ')');
}

static void
no_match (const char *func_name,
          union any_node **args, size_t arg_cnt,
          const struct operation *first, const struct operation *last)
{
  struct string s;
  const struct operation *f;

  ds_init_empty (&s);

  if (last - first == 1)
    {
      ds_put_format (&s, _("Type mismatch invoking %s as "), first->prototype);
      put_invocation (&s, func_name, args, arg_cnt);
    }
  else
    {
      ds_put_cstr (&s, _("Function invocation "));
      put_invocation (&s, func_name, args, arg_cnt);
      ds_put_cstr (&s, _(" does not match any known function.  Candidates are:"));

      for (f = first; f < last; f++)
        ds_put_format (&s, "\n%s", f->prototype);
    }
  ds_put_byte (&s, '.');

  msg (SE, "%s", ds_cstr (&s));

  ds_destroy (&s);
}

static union any_node *
parse_function (struct lexer *lexer, struct expression *e)
{
  int min_valid;
  const struct operation *f, *first, *last;

  union any_node **args = NULL;
  int arg_cnt = 0;
  int arg_cap = 0;

  struct string func_name;

  union any_node *n;

  ds_init_substring (&func_name, lex_tokss (lexer));
  min_valid = extract_min_valid (lex_tokcstr (lexer));
  if (!lookup_function (lex_tokcstr (lexer), &first, &last))
    {
      msg (SE, _("No function or vector named %s."), lex_tokcstr (lexer));
      ds_destroy (&func_name);
      return NULL;
    }

  lex_get (lexer);
  if (!lex_force_match (lexer, T_LPAREN))
    {
      ds_destroy (&func_name);
      return NULL;
    }

  args = NULL;
  arg_cnt = arg_cap = 0;
  if (lex_token (lexer) != T_RPAREN)
    for (;;)
      {
        if (lex_token (lexer) == T_ID
            && lex_next_token (lexer, 1) == T_TO)
          {
            const struct variable **vars;
            size_t var_cnt;
            size_t i;

            if (!parse_variables_const (lexer, dataset_dict (e->ds), &vars, &var_cnt, PV_SINGLE))
              goto fail;
            for (i = 0; i < var_cnt; i++)
              add_arg (&args, &arg_cnt, &arg_cap,
                       allocate_unary_variable (e, vars[i]));
            free (vars);
          }
        else
          {
            union any_node *arg = parse_or (lexer, e);
            if (arg == NULL)
              goto fail;

            add_arg (&args, &arg_cnt, &arg_cap, arg);
          }
        if (lex_match (lexer, T_RPAREN))
          break;
        else if (!lex_match (lexer, T_COMMA))
          {
            lex_error_expecting (lexer, "`,'", "`)'", NULL_SENTINEL);
            goto fail;
          }
      }

  for (f = first; f < last; f++)
    if (match_function (args, arg_cnt, f))
      break;
  if (f >= last)
    {
      no_match (ds_cstr (&func_name), args, arg_cnt, first, last);
      goto fail;
    }

  coerce_function_args (e, f, args, arg_cnt);
  if (!validate_function_args (f, arg_cnt, min_valid))
    goto fail;

  if ((f->flags & OPF_EXTENSION) && settings_get_syntax () == COMPATIBLE)
    msg (SW, _("%s is a PSPP extension."), f->prototype);
  if (f->flags & OPF_UNIMPLEMENTED)
    {
      msg (SE, _("%s is not available in this version of PSPP."),
           f->prototype);
      goto fail;
    }
  if ((f->flags & OPF_PERM_ONLY) &&
      proc_in_temporary_transformations (e->ds))
    {
      msg (SE, _("%s may not appear after %s."), f->prototype, "TEMPORARY");
      goto fail;
    }

  n = expr_allocate_composite (e, f - operations, args, arg_cnt);
  n->composite.min_valid = min_valid != -1 ? min_valid : f->array_min_elems;

  if (n->type == OP_LAG_Vn || n->type == OP_LAG_Vs)
    dataset_need_lag (e->ds, 1);
  else if (n->type == OP_LAG_Vnn || n->type == OP_LAG_Vsn)
    {
      int n_before;
      assert (n->composite.arg_cnt == 2);
      assert (n->composite.args[1]->type == OP_pos_int);
      n_before = n->composite.args[1]->integer.i;
      dataset_need_lag (e->ds, n_before);
    }

  free (args);
  ds_destroy (&func_name);
  return n;

fail:
  free (args);
  ds_destroy (&func_name);
  return NULL;
}

/* Utility functions. */

static struct expression *
expr_create (struct dataset *ds)
{
  struct pool *pool = pool_create ();
  struct expression *e = pool_alloc (pool, sizeof *e);
  e->expr_pool = pool;
  e->ds = ds;
  e->eval_pool = pool_create_subpool (e->expr_pool);
  e->ops = NULL;
  e->op_types = NULL;
  e->op_cnt = e->op_cap = 0;
  return e;
}

atom_type
expr_node_returns (const union any_node *n)
{
  assert (n != NULL);
  assert (is_operation (n->type));
  if (is_atom (n->type))
    return n->type;
  else if (is_composite (n->type))
    return operations[n->type].returns;
  else
    NOT_REACHED ();
}

static const char *
atom_type_name (atom_type type)
{
  assert (is_atom (type));
  return operations[type].name;
}

union any_node *
expr_allocate_nullary (struct expression *e, operation_type op)
{
  return expr_allocate_composite (e, op, NULL, 0);
}

union any_node *
expr_allocate_unary (struct expression *e, operation_type op,
                     union any_node *arg0)
{
  return expr_allocate_composite (e, op, &arg0, 1);
}

union any_node *
expr_allocate_binary (struct expression *e, operation_type op,
                      union any_node *arg0, union any_node *arg1)
{
  union any_node *args[2];
  args[0] = arg0;
  args[1] = arg1;
  return expr_allocate_composite (e, op, args, 2);
}

static bool
is_valid_node (union any_node *n)
{
  const struct operation *op;
  size_t i;

  assert (n != NULL);
  assert (is_operation (n->type));
  op = &operations[n->type];

  if (!is_atom (n->type))
    {
      struct composite_node *c = &n->composite;

      assert (is_composite (n->type));
      assert (c->arg_cnt >= op->arg_cnt);
      for (i = 0; i < op->arg_cnt; i++)
        assert (is_compatible (op->args[i], expr_node_returns (c->args[i])));
      if (c->arg_cnt > op->arg_cnt && !is_operator (n->type))
        {
          assert (op->flags & OPF_ARRAY_OPERAND);
          for (i = 0; i < c->arg_cnt; i++)
            assert (is_compatible (op->args[op->arg_cnt - 1],
                                   expr_node_returns (c->args[i])));
        }
    }

  return true;
}

union any_node *
expr_allocate_composite (struct expression *e, operation_type op,
                         union any_node **args, size_t arg_cnt)
{
  union any_node *n;
  size_t i;

  n = pool_alloc (e->expr_pool, sizeof n->composite);
  n->type = op;
  n->composite.arg_cnt = arg_cnt;
  n->composite.args = pool_alloc (e->expr_pool,
                                  sizeof *n->composite.args * arg_cnt);
  for (i = 0; i < arg_cnt; i++)
    {
      if (args[i] == NULL)
        return NULL;
      n->composite.args[i] = args[i];
    }
  memcpy (n->composite.args, args, sizeof *n->composite.args * arg_cnt);
  n->composite.min_valid = 0;
  assert (is_valid_node (n));
  return n;
}

union any_node *
expr_allocate_number (struct expression *e, double d)
{
  union any_node *n = pool_alloc (e->expr_pool, sizeof n->number);
  n->type = OP_number;
  n->number.n = d;
  return n;
}

union any_node *
expr_allocate_boolean (struct expression *e, double b)
{
  union any_node *n = pool_alloc (e->expr_pool, sizeof n->number);
  assert (b == 0.0 || b == 1.0 || b == SYSMIS);
  n->type = OP_boolean;
  n->number.n = b;
  return n;
}

union any_node *
expr_allocate_integer (struct expression *e, int i)
{
  union any_node *n = pool_alloc (e->expr_pool, sizeof n->integer);
  n->type = OP_integer;
  n->integer.i = i;
  return n;
}

union any_node *
expr_allocate_pos_int (struct expression *e, int i)
{
  union any_node *n = pool_alloc (e->expr_pool, sizeof n->integer);
  assert (i > 0);
  n->type = OP_pos_int;
  n->integer.i = i;
  return n;
}

union any_node *
expr_allocate_vector (struct expression *e, const struct vector *vector)
{
  union any_node *n = pool_alloc (e->expr_pool, sizeof n->vector);
  n->type = OP_vector;
  n->vector.v = vector;
  return n;
}

union any_node *
expr_allocate_string (struct expression *e, struct substring s)
{
  union any_node *n = pool_alloc (e->expr_pool, sizeof n->string);
  n->type = OP_string;
  n->string.s = s;
  return n;
}

union any_node *
expr_allocate_variable (struct expression *e, const struct variable *v)
{
  union any_node *n = pool_alloc (e->expr_pool, sizeof n->variable);
  n->type = var_is_numeric (v) ? OP_num_var : OP_str_var;
  n->variable.v = v;
  return n;
}

union any_node *
expr_allocate_format (struct expression *e, const struct fmt_spec *format)
{
  union any_node *n = pool_alloc (e->expr_pool, sizeof n->format);
  n->type = OP_format;
  n->format.f = *format;
  return n;
}

/* Allocates a unary composite node that represents the value of
   variable V in expression E. */
static union any_node *
allocate_unary_variable (struct expression *e, const struct variable *v)
{
  assert (v != NULL);
  return expr_allocate_unary (e, var_is_numeric (v) ? OP_NUM_VAR : OP_STR_VAR,
                              expr_allocate_variable (e, v));
}

/* Export function details to other modules. */

/* Returns the operation structure for the function with the
   given IDX. */
const struct operation *
expr_get_function (size_t idx)
{
  assert (idx < OP_function_cnt);
  return &operations[OP_function_first + idx];
}

/* Returns the number of expression functions. */
size_t
expr_get_function_cnt (void)
{
  return OP_function_cnt;
}

/* Returns the name of operation OP. */
const char *
expr_operation_get_name (const struct operation *op)
{
  return op->name;
}

/* Returns the human-readable prototype for operation OP. */
const char *
expr_operation_get_prototype (const struct operation *op)
{
  return op->prototype;
}

/* Returns the number of arguments for operation OP. */
int
expr_operation_get_arg_cnt (const struct operation *op)
{
  return op->arg_cnt;
}
