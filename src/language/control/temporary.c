/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011 Free Software Foundation, Inc.

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

#include <stddef.h>
#include <stdlib.h>

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/transformations.h"
#include "data/value-labels.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/control/control-stack.h"
#include "language/lexer/lexer.h"
#include "libpspp/message.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Parses the TEMPORARY command. */
int
cmd_temporary (struct lexer *lexer UNUSED, struct dataset *ds)
{
  if (!proc_in_temporary_transformations (ds))
    proc_start_temporary_transformations (ds);
  else
    msg (SE, _("This command may only appear once between "
               "procedures and procedure-like commands."));
  return CMD_SUCCESS;
}
