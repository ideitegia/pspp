/* PSPP - computes sample statistics.
   Copyright (C) 2004 Free Software Foundation, Inc.
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
#include <assert.h>
#include <stdio.h>
#include "command.h"
#include "error.h"
#include "expr.h"
#include "lexer.h"
#include "var.h"

int
cmd_debug_evaluate (void)
{
  struct expression *expr;
  union value value;
  enum expr_type expr_flags;
  int dump_postfix = 0;

  discard_variables ();

  expr_flags = 0;
  if (lex_match_id ("NOOPTIMIZE"))
    expr_flags |= EXPR_NO_OPTIMIZE;
  if (lex_match_id ("POSTFIX"))
    dump_postfix = 1;
  if (token != '/') 
    {
      lex_force_match ('/');
      return CMD_FAILURE;
    }
  fprintf (stderr, "%s => ", lex_rest_of_line (NULL));
  lex_get ();

  expr = expr_parse (EXPR_ANY | expr_flags);
  if (!expr || token != '.') 
    {
      fprintf (stderr, "error\n");
      return CMD_FAILURE; 
    }

  if (dump_postfix) 
    expr_debug_print_postfix (expr);
  else 
    {
      expr_evaluate (expr, NULL, 0, &value);
      switch (expr_get_type (expr)) 
        {
        case EXPR_NUMERIC:
          if (value.f == SYSMIS)
            fprintf (stderr, "sysmis\n");
          else
            fprintf (stderr, "%.2f\n", value.f);
          break;
      
        case EXPR_BOOLEAN:
          if (value.f == SYSMIS)
            fprintf (stderr, "sysmis\n");
          else if (value.f == 0.0)
            fprintf (stderr, "false\n");
          else
            fprintf (stderr, "true\n");
          break;

        case EXPR_STRING:
          fputc ('"', stderr);
          fwrite (value.c + 1, value.c[0], 1, stderr);
          fputs ("\"\n", stderr);
          break;

        default:
          assert (0);
        }
    }
  
  expr_free (expr);
  return CMD_SUCCESS;
}
