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
#include <stddef.h>
#include <stdlib.h>
#include "alloc.h"
#include "command.h"
#include "do-ifP.h"
#include "error.h"
#include "hash.h"
#include "lexer.h"
#include "str.h"
#include "value-labels.h"
#include "var.h"

#include "debug-print.h"

int temporary;
struct dictionary *temp_dict;
int temp_trns;

/* Parses the TEMPORARY command. */
int
cmd_temporary (void)
{
  lex_match_id ("TEMPORARY");

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

  /* Everything is temporary, even if we think it'll last forever.
     Especially then. */
  temporary = 1;
  temp_dict = save_dictionary ();
  if (f_trns == n_trns)
    temp_trns = -1;
  else
    temp_trns = n_trns;
  debug_printf (("TEMPORARY: temp_trns=%d\n", temp_trns));

  return lex_end_of_command ();
}

/* Copies a variable structure. */
void
copy_variable (struct variable *dest, const struct variable *src)
{
  int i, n;

  assert (dest != src);
  dest->type = src->type;
  dest->left = src->left;
  dest->width = src->width;
  dest->fv = src->fv;
  dest->nv = src->nv;
  dest->miss_type = src->miss_type;
  
  switch (src->miss_type)
    {
    case MISSING_NONE:
      n = 0;
      break;
    case MISSING_1:
      n = 1;
      break;
    case MISSING_2:
    case MISSING_RANGE:
      n = 2;
      break;
    case MISSING_3:
    case MISSING_RANGE_1:
      n = 3;
      break;
    default:
      assert (0);
      break;
    }
  
  for (i = 0; i < n; i++)
    dest->missing[i] = src->missing[i];
  dest->print = src->print;
  dest->write = src->write;

  dest->val_labs = val_labs_copy (src->val_labs);
  dest->label = src->label ? xstrdup (src->label) : NULL;
}

/* Returns a newly created empty dictionary.  The file label and
   documents are copied from default_dict if COPY is nonzero. */
struct dictionary *
new_dictionary (int copy)
{
  struct dictionary *d = xmalloc (sizeof *d);
  
  d->var = NULL;
  d->name_tab = hsh_create (8, compare_variables, hash_variable, NULL, NULL);
  d->nvar = 0;

  d->N = 0;

  d->nval = 0;

  d->n_splits = 0;
  d->splits = NULL;

  if (default_dict.label && copy)
    d->label = xstrdup (default_dict.label);
  else
    d->label = NULL;

  if (default_dict.n_documents && copy)
    {
      d->n_documents = default_dict.n_documents;
      if (d->n_documents)
	{
	  d->documents = malloc (default_dict.n_documents * 80);
	  memcpy (d->documents, default_dict.documents,
		  default_dict.n_documents * 80);
	}
    }
  else
    {
      d->n_documents = 0;
      d->documents = NULL;
    }
  
  d->weight_index = -1;
  d->weight_var[0] = 0;

  d->filter_var[0] = 0;

  return d;
}
    
/* Copies the current dictionary info into a newly allocated
   dictionary structure, which is returned. */
struct dictionary *
save_dictionary (void)
{
  /* Dictionary being created. */
  struct dictionary *d;

  int i;

  d = xmalloc (sizeof *d);

  /* First the easy stuff. */
  *d = default_dict;
  d->label = default_dict.label ? xstrdup (default_dict.label) : NULL;
  if (default_dict.n_documents)
    {
      d->documents = malloc (default_dict.n_documents * 80);
      memcpy (d->documents, default_dict.documents,
	      default_dict.n_documents * 80);
    }
  else d->documents = NULL;

  /* Then the variables. */
  d->name_tab = hsh_create (8, compare_variables, hash_variable, NULL, NULL);
  d->var = xmalloc (default_dict.nvar * sizeof *d->var);
  for (i = 0; i < default_dict.nvar; i++)
    {
      d->var[i] = xmalloc (sizeof *d->var[i]);
      copy_variable (d->var[i], default_dict.var[i]);
      strcpy (d->var[i]->name, default_dict.var[i]->name);
      d->var[i]->index = i;
      hsh_force_insert (d->name_tab, d->var[i]);
    }

  /* Then the SPLIT FILE variables. */
  if (default_dict.splits)
    {
      int i;

      d->n_splits = default_dict.n_splits;
      d->splits = xmalloc ((default_dict.n_splits + 1) * sizeof *d->splits);
      for (i = 0; i < default_dict.n_splits; i++)
	d->splits[i] = d->var[default_dict.splits[i]->index];
      d->splits[default_dict.n_splits] = NULL;
    }
  else
    {
      d->n_splits = 0;
      d->splits = NULL;
    }
  
  return d;
}

/* Copies dictionary D into the active file dictionary.  Deletes
   dictionary D. */
void
restore_dictionary (struct dictionary * d)
{
  int i;

  /* 1. Delete the current dictionary. */
  default_dict.n_splits = 0;
  free (default_dict.splits);
  default_dict.splits = NULL;
  
  hsh_destroy (default_dict.name_tab);
  default_dict.name_tab = NULL;
  
  for (i = 0; i < default_dict.nvar; i++)
    {
      clear_variable (&default_dict, default_dict.var[i]);
      free (default_dict.var[i]);
    }
  
  free (default_dict.var);
  free (default_dict.label);
  free (default_dict.documents);

  /* 2. Copy dictionary D into the active file dictionary. */
  default_dict = *d;
  if (default_dict.name_tab == NULL)
    {
      default_dict.name_tab = hsh_create (8, compare_variables, hash_variable,
                                          NULL, NULL);
      
      for (i = 0; i < default_dict.nvar; i++)
	hsh_force_insert (default_dict.name_tab, default_dict.var[i]);
    }

  /* 3. Destroy dictionary D. */
  free (d);
}

/* Destroys dictionary D. */
void
free_dictionary (struct dictionary * d)
{
  int i;

  d->n_splits = 0;
  free (d->splits);
  d->splits = NULL;
  
  if (d->name_tab)
    hsh_destroy (d->name_tab);

  for (i = 0; i < d->nvar; i++)
    {
      struct variable *v = d->var[i];

      val_labs_destroy (v->val_labs);
      if (v->label)
	{
	  free (v->label);
	  v->label = NULL;
	}
      free (d->var[i]);
    }
  free (d->var);

  free (d->label);
  free (d->documents);

  free (d);
}

/* Cancels the temporary transformation, if any. */
void
cancel_temporary (void)
{
  if (temporary)
    {
      if (temp_dict)
	free_dictionary (temp_dict);
      temporary = 0;
      temp_trns = 0;
    }
}
