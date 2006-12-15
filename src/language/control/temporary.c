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

#include <stddef.h>
#include <stdlib.h>

#include "control-stack.h"
#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/transformations.h>
#include <data/value-labels.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <libpspp/alloc.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses the TEMPORARY command. */
int
cmd_temporary (struct lexer *lexer, struct dataset *ds)
{
  if (!proc_in_temporary_transformations (ds))
    proc_start_temporary_transformations (ds);
  else
    msg (SE, _("This command may only appear once between "
               "procedures and procedure-like commands."));
  return lex_end_of_command (lexer);
}
