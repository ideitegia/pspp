/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2011 Free Software Foundation, Inc.

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

#include "language/expressions/private.h"

#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include "data/calendar.h"
#include "data/data-in.h"
#include "data/variable.h"
#include "evaluate.h"
#include "language/expressions/helpers.h"
#include "language/expressions/public.h"
#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/pool.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

static union any_node *evaluate_tree (struct composite_node *,
                                      struct expression *);
static union any_node *optimize_tree (union any_node *, struct expression *);

union any_node *
expr_optimize (union any_node *node, struct expression *e)
{
  int nonconst_cnt = 0; /* Number of nonconstant children. */
  int sysmis_cnt = 0;   /* Number of system-missing children. */
  const struct operation *op;
  struct composite_node *c;
  int i;

  /* We can't optimize an atom. */
  if (is_atom (node->type))
    return node;

  /* Start by optimizing all the children. */
  c = &node->composite;
  for (i = 0; i < c->arg_cnt; i++)
    {
      c->args[i] = expr_optimize (c->args[i], e);
      if (c->args[i]->type == OP_number)
	{
	  if (c->args[i]->number.n == SYSMIS)
	    sysmis_cnt++;
	}

      if (!is_atom (c->args[i]->type))
	nonconst_cnt++;
    }

  op = &operations[c->type];
  if (sysmis_cnt && (op->flags & OPF_ABSORB_MISS) == 0)
    {
      /* Most operations produce SYSMIS given any SYSMIS
         argument. */
      assert (op->returns == OP_number || op->returns == OP_boolean);
      if (op->returns == OP_number)
        return expr_allocate_number (e, SYSMIS);
      else
        return expr_allocate_boolean (e, SYSMIS);
    }
  else if (!nonconst_cnt && (op->flags & OPF_NONOPTIMIZABLE) == 0)
    {
      /* Evaluate constant expressions. */
      return evaluate_tree (&node->composite, e);
    }
  else
    {
      /* A few optimization possibilities are still left. */
      return optimize_tree (node, e);
    }
}

static int
eq_double (union any_node *node, double n)
{
  return node->type == OP_number && node->number.n == n;
}

static union any_node *
optimize_tree (union any_node *node, struct expression *e)
{
  struct composite_node *n = &node->composite;
  assert (is_composite (node->type));

  /* If you add to these optimizations, please also add a
     correctness test in tests/expressions/expressions.sh. */

  /* x+0, x-0, 0+x => x. */
  if ((n->type == OP_ADD || n->type == OP_SUB) && eq_double (n->args[1], 0.))
    return n->args[0];
  else if (n->type == OP_ADD && eq_double (n->args[0], 0.))
    return n->args[1];

  /* x*1, x/1, 1*x => x. */
  else if ((n->type == OP_MUL || n->type == OP_DIV)
           && eq_double (n->args[1], 1.))
    return n->args[0];
  else if (n->type == OP_MUL && eq_double (n->args[0], 1.))
    return n->args[1];

  /* 0*x, 0/x, x*0, MOD(0,x) => 0. */
  else if (((n->type == OP_MUL || n->type == OP_DIV || n->type == OP_MOD_nn)
            && eq_double (n->args[0], 0.))
           || (n->type == OP_MUL && eq_double (n->args[1], 0.)))
    return expr_allocate_number (e, 0.);

  /* x**1 => x. */
  else if (n->type == OP_POW && eq_double (n->args[1], 1))
    return n->args[0];

  /* x**2 => SQUARE(x). */
  else if (n->type == OP_POW && eq_double (n->args[1], 2))
    return expr_allocate_unary (e, OP_SQUARE, n->args[0]);

  /* Otherwise, nothing to do. */
  else
    return node;
}

static double get_number_arg (struct composite_node *, size_t arg_idx);
static double *get_number_args (struct composite_node *,
                                 size_t arg_idx, size_t arg_cnt,
                                 struct expression *);
static struct substring get_string_arg (struct composite_node *,
                                           size_t arg_idx);
static struct substring *get_string_args (struct composite_node *,
                                             size_t arg_idx, size_t arg_cnt,
                                             struct expression *);
static const struct fmt_spec *get_format_arg (struct composite_node *,
                                              size_t arg_idx);

static union any_node *
evaluate_tree (struct composite_node *node, struct expression *e)
{
  switch (node->type)
    {
#include "optimize.inc"

    default:
      NOT_REACHED ();
    }

  NOT_REACHED ();
}

static double
get_number_arg (struct composite_node *c, size_t arg_idx)
{
  assert (arg_idx < c->arg_cnt);
  assert (c->args[arg_idx]->type == OP_number
          || c->args[arg_idx]->type == OP_boolean);
  return c->args[arg_idx]->number.n;
}

static double *
get_number_args (struct composite_node *c, size_t arg_idx, size_t arg_cnt,
                 struct expression *e)
{
  double *d;
  size_t i;

  d = pool_alloc (e->expr_pool, sizeof *d * arg_cnt);
  for (i = 0; i < arg_cnt; i++)
    d[i] = get_number_arg (c, i + arg_idx);
  return d;
}

static struct substring
get_string_arg (struct composite_node *c, size_t arg_idx)
{
  assert (arg_idx < c->arg_cnt);
  assert (c->args[arg_idx]->type == OP_string);
  return c->args[arg_idx]->string.s;
}

static struct substring *
get_string_args (struct composite_node *c, size_t arg_idx, size_t arg_cnt,
                 struct expression *e)
{
  struct substring *s;
  size_t i;

  s = pool_alloc (e->expr_pool, sizeof *s * arg_cnt);
  for (i = 0; i < arg_cnt; i++)
    s[i] = get_string_arg (c, i + arg_idx);
  return s;
}

static const struct fmt_spec *
get_format_arg (struct composite_node *c, size_t arg_idx)
{
  assert (arg_idx < c->arg_cnt);
  assert (c->args[arg_idx]->type == OP_ni_format
          || c->args[arg_idx]->type == OP_no_format);
  return &c->args[arg_idx]->format.f;
}

/* Expression flattening. */

static union operation_data *allocate_aux (struct expression *,
                                                operation_type);
static void flatten_node (union any_node *, struct expression *);

static void
emit_operation (struct expression *e, operation_type type)
{
  allocate_aux (e, OP_operation)->operation = type;
}

static void
emit_number (struct expression *e, double n)
{
  allocate_aux (e, OP_number)->number = n;
}

static void
emit_string (struct expression *e, struct substring s)
{
  allocate_aux (e, OP_string)->string = s;
}

static void
emit_format (struct expression *e, const struct fmt_spec *f)
{
  allocate_aux (e, OP_format)->format = pool_clone (e->expr_pool,
                                                    f, sizeof *f);
}

static void
emit_variable (struct expression *e, const struct variable *v)
{
  allocate_aux (e, OP_variable)->variable = v;
}

static void
emit_vector (struct expression *e, const struct vector *v)
{
  allocate_aux (e, OP_vector)->vector = v;
}

static void
emit_integer (struct expression *e, int i)
{
  allocate_aux (e, OP_integer)->integer = i;
}

void
expr_flatten (union any_node *n, struct expression *e)
{
  flatten_node (n, e);
  e->type = expr_node_returns (n);
  emit_operation (e, (e->type == OP_string
                      ? OP_return_string : OP_return_number));
}

static void
flatten_atom (union any_node *n, struct expression *e)
{
  switch (n->type)
    {
    case OP_number:
    case OP_boolean:
      emit_operation (e, OP_number);
      emit_number (e, n->number.n);
      break;

    case OP_string:
      emit_operation (e, OP_string);
      emit_string (e, n->string.s);
      break;

    case OP_num_var:
    case OP_str_var:
    case OP_vector:
    case OP_no_format:
    case OP_ni_format:
    case OP_pos_int:
      /* These are passed as aux data following the
         operation. */
      break;

    default:
      NOT_REACHED ();
    }
}

static void
flatten_composite (union any_node *n, struct expression *e)
{
  const struct operation *op = &operations[n->type];
  size_t i;

  for (i = 0; i < n->composite.arg_cnt; i++)
    flatten_node (n->composite.args[i], e);

  if (n->type != OP_BOOLEAN_TO_NUM)
    emit_operation (e, n->type);

  for (i = 0; i < n->composite.arg_cnt; i++)
    {
      union any_node *arg = n->composite.args[i];
      switch (arg->type)
        {
        case OP_num_var:
        case OP_str_var:
          emit_variable (e, arg->variable.v);
          break;

        case OP_vector:
          emit_vector (e, arg->vector.v);
          break;

        case OP_ni_format:
        case OP_no_format:
          emit_format (e, &arg->format.f);
          break;

        case OP_pos_int:
          emit_integer (e, arg->integer.i);
          break;

        default:
          /* Nothing to do. */
          break;
        }
    }

  if (op->flags & OPF_ARRAY_OPERAND)
    emit_integer (e, n->composite.arg_cnt - op->arg_cnt + 1);
  if (op->flags & OPF_MIN_VALID)
    emit_integer (e, n->composite.min_valid);
}

void
flatten_node (union any_node *n, struct expression *e)
{
  assert (is_operation (n->type));

  if (is_atom (n->type))
    flatten_atom (n, e);
  else if (is_composite (n->type))
    flatten_composite (n, e);
  else
    NOT_REACHED ();
}

static union operation_data *
allocate_aux (struct expression *e, operation_type type)
{
  if (e->op_cnt >= e->op_cap)
    {
      e->op_cap = (e->op_cap + 8) * 3 / 2;
      e->ops = pool_realloc (e->expr_pool, e->ops, sizeof *e->ops * e->op_cap);
      e->op_types = pool_realloc (e->expr_pool, e->op_types,
                                  sizeof *e->op_types * e->op_cap);
    }

  e->op_types[e->op_cnt] = type;
  return &e->ops[e->op_cnt++];
}
