/* PSPP - computes sample statistics.
   Copyright (C) 1997-9, 2000, 2006 Free Software Foundation, Inc.

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
#include "private.h"

#include <ctype.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/message.h>
#include "helpers.h"
#include "evaluate.h"
#include <libpspp/pool.h>

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
          *(double *) result = finite (ns[-1]) ? ns[-1] : SYSMIS;
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
  
  buf_copy_rpad (dst, dst_size, s.string, s.length);
}

#include <language/lexer/lexer.h>
#include <language/command.h>

int
cmd_debug_evaluate (struct lexer *lexer, struct dataset *dsother UNUSED)
{
  bool optimize = true;
  int retval = CMD_FAILURE;
  bool dump_postfix = false;

  struct ccase *c = NULL;

  struct dataset *ds = NULL;

  struct expression *expr;

  for (;;) 
    {
      struct dictionary *d = NULL;
      if (lex_match_id (lexer, "NOOPTIMIZE"))
        optimize = 0;
      else if (lex_match_id (lexer, "POSTFIX"))
        dump_postfix = 1;
      else if (lex_match (lexer, '('))
        {
          char name[LONG_NAME_LEN + 1];
          struct variable *v;
          size_t old_value_cnt;
          int width;

          if (!lex_force_id (lexer))
            goto done;
          strcpy (name, lex_tokid (lexer));

          lex_get (lexer);
          if (!lex_force_match (lexer, '='))
            goto done;

          if (lex_is_number (lexer))
            {
              width = 0;
              fprintf (stderr, "(%s = %.2f)", name, lex_tokval (lexer)); 
            }
          else if (lex_token (lexer) == T_STRING) 
            {
              width = ds_length (lex_tokstr (lexer));
              fprintf (stderr, "(%s = \"%.2s\")", name, ds_cstr (lex_tokstr (lexer))); 
            }
          else
            {
              lex_error (lexer, _("expecting number or string"));
              goto done;
            }

	  if  ( ds == NULL )
	    {
	      ds = create_dataset ();
	      d = dataset_dict (ds);
	    }

          old_value_cnt = dict_get_next_value_idx (d);
          v = dict_create_var (d, name, width);
          if (v == NULL)
            {
              msg (SE, _("Duplicate variable name %s."), name);
              goto done;
            }

          if (c == NULL) 
            {
              c = xmalloc (sizeof *c);
              case_create (c, dict_get_next_value_idx (d));
            }
          else
            case_resize (c, old_value_cnt, dict_get_next_value_idx (d));

          if (lex_is_number (lexer))
            case_data_rw (c, v)->f = lex_tokval (lexer);
          else
            memcpy (case_data_rw (c, v)->s, ds_data (lex_tokstr (lexer)),
                    var_get_width (v));
          lex_get (lexer);

          if (!lex_force_match (lexer, ')'))
            goto done;
        }
      else 
        break;
    }
  if (lex_token (lexer) != '/') 
    {
      lex_force_match (lexer, '/');
      goto done;
    }

  if ( ds != NULL ) 
    fprintf(stderr, "; ");
  fprintf (stderr, "%s => ", lex_rest_of_line (lexer, NULL));
  lex_get (lexer);

  expr = expr_parse_any (lexer, ds, optimize);
  if (!expr || lex_end_of_command (lexer) != CMD_SUCCESS)
    {
      if (expr != NULL)
        expr_free (expr);
      fprintf (stderr, "error\n");
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
            fprintf (stderr, "sysmis\n");
          else
            fprintf (stderr, "%.2f\n", d); 
        }
        break;
      
      case OP_boolean: 
        {
          double b = expr_evaluate_num (expr, c, 0);
          fprintf (stderr, "%s\n",
                   b == SYSMIS ? "sysmis" : b == 0.0 ? "false" : "true"); 
        }
        break;

      case OP_string: 
        {
          struct substring s;
          expr_evaluate (expr, c, 0, &s);

          fputc ('"', stderr);
          fwrite (s.string, s.length, 1, stderr);
          fputs ("\"\n", stderr);
          break; 
        }

      default:
        NOT_REACHED ();
      }

  expr_free (expr);
  retval = CMD_SUCCESS;

 done:
  if (ds)
    destroy_dataset (ds);

  if (c != NULL) 
    {
      case_destroy (c);
      free (c); 
    }

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
            fprintf (stderr, "return_number");
          else if (op->operation == OP_return_string)
            fprintf (stderr, "return_string");
          else if (is_function (op->operation)) 
            fprintf (stderr, "%s", operations[op->operation].prototype);
          else if (is_composite (op->operation)) 
            fprintf (stderr, "%s", operations[op->operation].name);
          else
            fprintf (stderr, "%s:", operations[op->operation].name);
          break;
        case OP_number:
          if (op->number != SYSMIS)
            fprintf (stderr, "n<%g>", op->number);
          else
            fprintf (stderr, "n<SYSMIS>");
          break;
        case OP_string:
          fprintf (stderr, "s<%.*s>",
                   (int) op->string.length,
                   op->string.string != NULL ? op->string.string : "");
          break;
        case OP_format:
          {
            char str[FMT_STRING_LEN_MAX + 1];
            fmt_to_string (op->format, str);
            fprintf (stderr, "f<%s>", str); 
          }
          break;
        case OP_variable:
          fprintf (stderr, "v<%s>", var_get_name (op->variable));
          break;
        case OP_vector:
          fprintf (stderr, "vec<%s>", vector_get_name (op->vector));
          break;
        case OP_integer:
          fprintf (stderr, "i<%d>", op->integer);
          break;
        default:
          NOT_REACHED ();
        } 
    }
  fprintf (stderr, "\n");
}
