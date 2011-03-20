/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2006, 2007, 2009, 2010, 2011 Free Software Foundation, Inc.

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

#include "language/expressions/evaluate.h"

#include <ctype.h>

#include "libpspp/assertion.h"
#include "libpspp/message.h"
#include "language/expressions/helpers.h"
#include "language/expressions/private.h"
#include "language/lexer/value-parser.h"
#include "libpspp/pool.h"

#include "xalloc.h"

static void
expr_evaluate (struct expression *e, const struct ccase *c, int case_idx,
               void *result)
{
  struct dataset *ds = e->ds;
  union operation_data *op = e->ops;

  double *ns = e->number_stack;
  struct substring *ss = e->string_stack;

  /* Without a dictionary/dataset, the expression can't refer to variables,
     and you don't need to specify a case when you evaluate the
     expression.  With a dictionary/dataset, the expression can refer
     to variables, so you must specify a case when you evaluate the
     expression. */
  assert ((c != NULL) == (e->ds != NULL));

  pool_clear (e->eval_pool);

  for (;;)
    {
      assert (op < e->ops + e->op_cnt);
      switch (op++->operation)
	{
        case OP_number:
        case OP_boolean:
          *ns++ = op++->number;
          break;

        case OP_string:
          {
            const struct substring *s = &op++->string;
            *ss++ = copy_string (e, s->string, s->length);
          }
          break;

        case OP_return_number:
          *(double *) result = isfinite (ns[-1]) ? ns[-1] : SYSMIS;
          return;

        case OP_return_string:
          *(struct substring *) result = ss[-1];
          return;

#include "evaluate.inc"

	default:
	  NOT_REACHED ();
	}
    }
}

double
expr_evaluate_num (struct expression *e, const struct ccase *c, int case_idx)
{
  double d;

  assert (e->type == OP_number || e->type == OP_boolean);
  expr_evaluate (e, c, case_idx, &d);
  return d;
}

void
expr_evaluate_str (struct expression *e, const struct ccase *c, int case_idx,
                   char *dst, size_t dst_size)
{
  struct substring s;

  assert (e->type == OP_string);
  assert ((dst == NULL) == (dst_size == 0));
  expr_evaluate (e, c, case_idx, &s);

  buf_copy_rpad (dst, dst_size, s.string, s.length, ' ');
}

#include "language/lexer/lexer.h"
#include "language/command.h"

int
cmd_debug_evaluate (struct lexer *lexer, struct dataset *dsother UNUSED)
{
  bool optimize = true;
  int retval = CMD_FAILURE;
  bool dump_postfix = false;

  struct ccase *c = NULL;

  struct dataset *ds = NULL;

  char *name = NULL;

  struct expression *expr;

  for (;;)
    {
      struct dictionary *d = NULL;
      if (lex_match_id (lexer, "NOOPTIMIZE"))
        optimize = 0;
      else if (lex_match_id (lexer, "POSTFIX"))
        dump_postfix = 1;
      else if (lex_match (lexer, T_LPAREN))
        {
          struct variable *v;
          size_t old_value_cnt;
          int width;

          if (!lex_force_id (lexer))
            goto done;
          name = xstrdup (lex_tokcstr (lexer));

          lex_get (lexer);
          if (!lex_force_match (lexer, T_EQUALS))
            goto done;

          if (lex_is_number (lexer))
            width = 0;
          else if (lex_is_string (lexer))
            width = ss_length (lex_tokss (lexer));
          else
            {
              lex_error (lexer, _("expecting number or string"));
              goto done;
            }

	  if  ( ds == NULL )
	    {
	      ds = dataset_create ();
	      d = dataset_dict (ds);
	    }

          old_value_cnt = dict_get_next_value_idx (d);
          v = dict_create_var (d, name, width);
          if (v == NULL)
            {
              msg (SE, _("Duplicate variable name %s."), name);
              goto done;
            }
          free (name);
          name = NULL;

          if (c == NULL)
            c = case_create (dict_get_proto (d));
          else
            c = case_unshare_and_resize (c, dict_get_proto (d));

          if (!parse_value (lexer, case_data_rw (c, v), var_get_width (v)))
            NOT_REACHED ();

          if (!lex_force_match (lexer, T_RPAREN))
            goto done;
        }
      else
        break;
    }
  if (lex_token (lexer) != T_SLASH)
    {
      lex_force_match (lexer, T_SLASH);
      goto done;
    }

  lex_get (lexer);

  expr = expr_parse_any (lexer, ds, optimize);
  if (!expr || lex_end_of_command (lexer) != CMD_SUCCESS)
    {
      if (expr != NULL)
        expr_free (expr);
      printf ("error\n");
      goto done;
    }

  if (dump_postfix)
    expr_debug_print_postfix (expr);
  else
    switch (expr->type)
      {
      case OP_number:
        {
          double d = expr_evaluate_num (expr, c, 0);
          if (d == SYSMIS)
            printf ("sysmis\n");
          else
            printf ("%.2f\n", d);
        }
        break;

      case OP_boolean:
        {
          double b = expr_evaluate_num (expr, c, 0);
          printf ("%s\n",
                   b == SYSMIS ? "sysmis" : b == 0.0 ? "false" : "true");
        }
        break;

      case OP_string:
        {
          struct substring s;
          expr_evaluate (expr, c, 0, &s);

          putchar ('"');
          fwrite (s.string, s.length, 1, stdout);
          puts ("\"");
          break;
        }

      default:
        NOT_REACHED ();
      }

  expr_free (expr);
  retval = CMD_SUCCESS;

 done:
  dataset_destroy (ds);

  case_unref (c);

  free (name);

  return retval;
}

void
expr_debug_print_postfix (const struct expression *e)
{
  size_t i;

  for (i = 0; i < e->op_cnt; i++)
    {
      union operation_data *op = &e->ops[i];
      if (i > 0)
        putc (' ', stderr);
      switch (e->op_types[i])
        {
        case OP_operation:
          if (op->operation == OP_return_number)
            printf ("return_number");
          else if (op->operation == OP_return_string)
            printf ("return_string");
          else if (is_function (op->operation))
            printf ("%s", operations[op->operation].prototype);
          else if (is_composite (op->operation))
            printf ("%s", operations[op->operation].name);
          else
            printf ("%s:", operations[op->operation].name);
          break;
        case OP_number:
          if (op->number != SYSMIS)
            printf ("n<%g>", op->number);
          else
            printf ("n<SYSMIS>");
          break;
        case OP_string:
          printf ("s<%.*s>",
                   (int) op->string.length,
                   op->string.string != NULL ? op->string.string : "");
          break;
        case OP_format:
          {
            char str[FMT_STRING_LEN_MAX + 1];
            fmt_to_string (op->format, str);
            printf ("f<%s>", str);
          }
          break;
        case OP_variable:
          printf ("v<%s>", var_get_name (op->variable));
          break;
        case OP_vector:
          printf ("vec<%s>", vector_get_name (op->vector));
          break;
        case OP_integer:
          printf ("i<%d>", op->integer);
          break;
        default:
          NOT_REACHED ();
        }
    }
  printf ("\n");
}
