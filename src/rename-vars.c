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
#include <stdlib.h>
#include <assert.h>
#include "alloc.h"
#include "command.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "str.h"
#include "var.h"

/* FIXME: should change weighting variable, etc. */
static int compare_name (const void *, const void *);

/* The code for this function is very similar to the code for the
   RENAME subcommand of MODIFY VARS. */
int
cmd_rename_variables (void)
{
  char (*names)[8] = NULL;

  struct variable **old_names = NULL;
  char **new_names = NULL;
  int n_rename = 0;

  struct variable *head, *tail, *iter;

  int i;

  lex_match_id ("RENAME");
  lex_match_id ("VARIABLES");

  do
    {
      int prev_nv_1 = n_rename;
      int prev_nv_2 = n_rename;

      if (!lex_match ('('))
	{
	  msg (SE, _("`(' expected."));
	  goto lossage;
	}
      if (!parse_variables (&default_dict, &old_names, &n_rename,
			    PV_APPEND | PV_NO_DUPLICATE))
	goto lossage;
      if (!lex_match ('='))
	{
	  msg (SE, _("`=' expected between lists of new and old variable names."));
	  goto lossage;
	}
      if (!parse_DATA_LIST_vars (&new_names, &prev_nv_1, PV_APPEND))
	goto lossage;
      if (prev_nv_1 != n_rename)
	{
	  msg (SE, _("Differing number of variables in old name list "
	       "(%d) and in new name list (%d)."),
	       n_rename - prev_nv_2, prev_nv_1 - prev_nv_2);
	  for (i = 0; i < prev_nv_1; i++)
	    free (new_names[i]);
	  free (new_names);
	  new_names = NULL;
	  goto lossage;
	}
      if (!lex_match (')'))
	{
	  msg (SE, _("`)' expected after variable names."));
	  goto lossage;
	}
    }
  while (token != '.');

  /* Form a linked list of the variables to be renamed; also, set
     their p.mfv.new_name members. */
  head = NULL;
  for (i = 0; i < n_rename; i++)
    {
      strcpy (old_names[i]->p.mfv.new_name, new_names[i]);
      free (new_names[i]);
      if (head != NULL)
	tail = tail->p.mfv.next = old_names[i];
      else
	head = tail = old_names[i];
    }
  tail->p.mfv.next = NULL;
  free (new_names);
  free (old_names);
  new_names = NULL;
  old_names = NULL;

  /* Construct a vector of all variables' new names. */
  names = xmalloc (8 * default_dict.nvar);
  for (i = 0; i < default_dict.nvar; i++)
    strncpy (names[i], default_dict.var[i]->name, 8);
  for (iter = head; iter; iter = iter->p.mfv.next)
    strncpy (names[iter->index], iter->p.mfv.new_name, 8);

  /* Sort the vector, then check for duplicates. */
  qsort (names, default_dict.nvar, 8, compare_name);
  for (i = 1; i < default_dict.nvar; i++)
    if (memcmp (names[i], names[i - 1], 8) == 0)
      {
	char name[9];
	strncpy (name, names[i], 8);
	name[8] = 0;
	msg (SE, _("Duplicate variable name `%s' after renaming."), name);
	goto lossage;
      }
  free (names);

  /* Finally, do the renaming. */
  for (iter = head; iter; iter = iter->p.mfv.next)
    hsh_force_delete (default_dict.name_tab, iter);
  for (iter = head; iter; iter = iter->p.mfv.next)
    {
      strcpy (iter->name, iter->p.mfv.new_name);
      hsh_force_insert (default_dict.name_tab, iter);
    }

  return CMD_SUCCESS;

lossage:
  if (new_names)
    for (i = 0; i < n_rename; i++)
      free (new_names[i]);
  free (new_names);
  free (old_names);
  free (names);
  return CMD_FAILURE;
}

static int
compare_name (const void *a, const void *b)
{
  return memcmp (a, b, 8);
}
