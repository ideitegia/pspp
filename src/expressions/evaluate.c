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
#include "private.h"

#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#include <ctype.h>
#include "alloc.h"
#include "error.h"
#include "helpers.h"
#include "evaluate.h"
#include "pool.h"

static void
expr_evaluate (struct expression *e, const struct ccase *c, int case_idx,
               void *result)
{
  union operation_data *op = e->ops;

  double *ns = e->number_stack;
  struct fixed_string *ss = e->string_stack;

  assert ((c != NULL) == (e->dict != NULL));
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
            const struct fixed_string *s = &op++->string;
            *ss++ = copy_string (e, s->string, s->length);
          }
          break;

        case OP_return_number:
          *(double *) result = finite (ns[-1]) ? ns[-1] : SYSMIS;
          return;

        case OP_return_string:
          *(struct fixed_string *) result = ss[-1];
          return;

#include "evaluate.inc"
          
	default:
	  abort ();
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
  struct fixed_string s;

  assert (e->type == OP_string);
  assert ((dst == NULL) == (dst_size == 0));
  expr_evaluate (e, c, case_idx, &s);
  st_bare_pad_len_copy (dst, s.string, dst_size, s.length);
}

#include "lexer.h"
#include "command.h"

int
cmd_debug_evaluate (void)
{
  bool optimize = true;
  int retval = CMD_FAILURE;
  bool dump_postfix = false;
  struct dictionary *d = NULL;
  struct ccase *c = NULL;

  struct expression *expr;

  for (;;) 
    {
      if (lex_match_id ("NOOPTIMIZE"))
        optimize = 0;
      else if (lex_match_id ("POSTFIX"))
        dump_postfix = 1;
      else if (lex_match ('('))
        {
          char name[MAX_VAR_NAME_LEN + 1];
          struct variable *v;
          size_t old_value_cnt;
          int width;

          if (!lex_force_id ())
            goto done;
          strcpy (name, tokid);

          lex_get ();
          if (!lex_force_match ('='))
            goto done;

          if (lex_is_number ())
            {
              width = 0;
              fprintf (stderr, "(%s = %.2f)", name, tokval); 
            }
          else if (token == T_STRING) 
            {
              width = ds_length (&tokstr);
              fprintf (stderr, "(%s = \"%.2s\")", name, ds_c_str (&tokstr)); 
            }
          else
            {
              lex_error (_("expecting number or string"));
              goto done;
            }

          if (d == NULL)
            d = dict_create ();
          
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
              case_nullify (c);
            }
          case_resize (c, old_value_cnt, dict_get_next_value_idx (d));

          if (lex_is_number ())
            case_data_rw (c, v->fv)->f = tokval;
          else
            memcpy (case_data_rw (c, v->fv)->s, ds_data (&tokstr),
                    v->width);
          lex_get ();

          if (!lex_force_match (')'))
            goto done;
        }
      else 
        break;
    }
  if (token != '/') 
    {
      lex_force_match ('/');
      goto done;
    }
  if (d != NULL)
    fprintf (stderr, "; ");
  fprintf (stderr, "%s => ", lex_rest_of_line (NULL));
  lex_get ();

  expr = expr_parse_any (d, optimize);
  if (!expr || lex_end_of_command () != CMD_SUCCESS)
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
          struct fixed_string s;
          expr_evaluate (expr, c, 0, &s);

          fputc ('"', stderr);
          fwrite (s.string, s.length, 1, stderr);
          fputs ("\"\n", stderr);
          break; 
        }

      default:
        assert (0);
      }

  expr_free (expr);
  retval = CMD_SUCCESS;

 done:
  if (c != NULL) 
    {
      case_destroy (c);
      free (c); 
    }
  dict_destroy (d);
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
                   (int) op->string.length, op->string.string);
          break;
        case OP_format:
          fprintf (stderr, "f<%s%d.%d>",
                  formats[op->format->type].name,
                  op->format->w, op->format->d);
          break;
        case OP_variable:
          fprintf (stderr, "v<%s>", op->variable->name);
          break;
        case OP_vector:
          fprintf (stderr, "vec<%s>", op->vector->name);
          break;
        case OP_integer:
          fprintf (stderr, "i<%d>", op->integer);
          break;
        default:
          abort ();
        } 
    }
  fprintf (stderr, "\n");
}
