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
#include "command.h"
#include "error.h"
#include "expr.h"
#include "lexer.h"
#include "var.h"

int
cmd_debug_evaluate (void)
{
  struct expression *expr;

  discard_variables ();
  expr = expr_parse (PXP_NONE);
  if (!expr)
    return CMD_FAILURE;

  expr_free (expr);
  if (token != '.')
    {
      msg (SE, _("Extra characters after expression."));
      return CMD_FAILURE;
    }
  
  return CMD_SUCCESS;
}
