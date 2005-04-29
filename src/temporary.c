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
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA. */

#include <config.h>
#include "error.h"
#include <stddef.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "dictionary.h"
#include "do-ifP.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"

int temporary;
struct dictionary *temp_dict;
int temp_trns;

/* Parses the TEMPORARY command. */
int
cmd_temporary (void)
{
  /* TEMPORARY is not allowed inside DO IF or LOOP. */
  if (ctl_stack)
    {
      msg (SE, _("This command is not valid inside DO IF or LOOP."));
      return CMD_FAILURE;
    }

  /* TEMPORARY can only appear once! */
  if (temporary)
    {
      msg (SE, _("This command may only appear once between "
	   "procedures and procedure-like commands."));
      return CMD_FAILURE;
    }

  /* Make a copy of the current dictionary. */
  temporary = 1;
  temp_dict = dict_clone (default_dict);
  temp_trns = n_trns;

  return lex_end_of_command ();
}

/* Cancels the temporary transformation, if any. */
void
cancel_temporary (void)
{
  if (temporary)
    {
      if (temp_dict) 
        {
          dict_destroy (temp_dict);
          temp_dict = NULL; 
        }
      temporary = 0;
      temp_trns = 0;
    }
}
