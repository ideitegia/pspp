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
#include <assert.h>
#include <stdio.h>
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

/* Notes:

   If the weighting variable is deleted somehow (for instance by
   end-of-scope of TEMPORARY), weighting must be canceled.

   Scratch vars may not be used for weighting. */

/* WEIGHT transformation. */
struct weight_trns
  {
    struct trns_header h;
    int src;			/* `value' index of weighting variable. */
    int dest;			/* `value' index of $WEIGHT. */
  };

int
cmd_weight (void)
{
  lex_match_id ("WEIGHT");

  if (lex_match_id ("OFF"))
    default_dict.weight_var[0] = 0;
  else
    {
      struct variable *v;

      lex_match (T_BY);
      v = parse_variable ();
      if (!v)
	return CMD_FAILURE;
      if (v->type == ALPHA)
	{
	  msg (SE, _("The weighting variable must be numeric."));
	  return CMD_FAILURE;
	}
      if (v->name[0] == '#')
	{
	  msg (SE, _("The weighting variable may not be scratch."));
	  return CMD_FAILURE;
	}

      strcpy (default_dict.weight_var, v->name);
    }

  return lex_end_of_command ();
}

#if 0 /* FIXME: dead code. */
static int
weight_trns_proc (any_trns * pt, ccase * c)
{
  weight_trns *t = (weight_trns *) pt;

  c->data[t->dest].f = c->data[t->src].f;
  return -1;
}
#endif

/* Global functions. */ 

/* Sets the weight_index member of dictionary D to an appropriate
   value for the value of weight_var, and returns the weighting
   variable if any or NULL if none. */
struct variable *
update_weighting (struct dictionary * d)
{
  if (d->weight_var[0])
    {
      struct variable *v = find_dict_variable (d, d->weight_var);
      if (v && v->type == NUMERIC)
	{
	  d->weight_index = v->fv;
	  return v;
	}
      else
	{
#if GLOBAL_DEBUGGING
	  printf (_("bad weighting variable, canceling\n"));
#endif
	  d->weight_var[0] = 0;
	}
    }

  d->weight_index = -1;
  return NULL;
}

/* Turns off case weighting for dictionary D. */
void
stop_weighting (struct dictionary * d)
{
  d->weight_var[0] = 0;
}
