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

/* AIX requires this to be the first thing in the file.  */
#include <config.h>
#if __GNUC__
#define alloca __builtin_alloca
#else
#if HAVE_ALLOCA_H
#include <alloca.h>
#else
#ifdef _AIX
#pragma alloca
#else
#ifndef alloca			/* predefined by HP cc +Olibcalls */
char *alloca ();
#endif
#endif
#endif
#endif

#include <stdlib.h>
#include <assert.h>
#include "alloc.h"
#include "avl.h"
#include "bitvector.h"
#include "command.h"
#include "error.h"
#include "lexer.h"
#include "misc.h"
#include "str.h"
#include "var.h"
#include "vfm.h"

/* FIXME: should change weighting variable, etc. */
/* These control the way that compare_variables() does its work. */
static int forward;		/* 1=FORWARD, 0=BACKWARD. */
static int positional;		/* 1=POSITIONAL, 0=ALPHA. */

static int compare_variables (const void *pa, const void *pb);

/* Explains how to modify the variables in a dictionary in conjunction
   with the p.mfv field of `variable'. */
struct var_modification
  {
    /* REORDER information. */
    struct variable **reorder_list;

    /* RENAME information. */
    struct variable **old_names;
    char **new_names;
    int n_rename;

    /* DROP/KEEP information. */
    int n_drop;			/* Number of variables being dropped. */
  };

static struct dictionary *rearrange_dict (struct dictionary *d,
					  struct var_modification *vm,
					  int permanent);

/* Performs MODIFY VARS command. */
int
cmd_modify_vars (void)
{
  /* Bits indicated whether we've already encountered a subcommand of
     this type. */
  unsigned already_encountered = 0;

  /* What we're gonna do to the active file. */
  struct var_modification vm;

  lex_match_id ("MODIFY");
  lex_match_id ("VARS");

  vm.reorder_list = NULL;
  vm.old_names = NULL;
  vm.new_names = NULL;
  vm.n_rename = 0;
  vm.n_drop = 0;

  /* Parse each subcommand. */
  lex_match ('/');
  for (;;)
    {
      if (lex_match_id ("REORDER"))
	{
	  struct variable **v = NULL;
	  int nv = 0;

	  if (already_encountered & 1)
	    {
	      msg (SE, _("REORDER subcommand may be given at most once."));
	      goto lossage;
	    }
	  already_encountered |= 1;

	  lex_match ('=');
	  do
	    {
	      int prev_nv = nv;

	      forward = positional = 1;
	      if (lex_match_id ("FORWARD"));
	      else if (lex_match_id ("BACKWARD"))
		forward = 0;
	      if (lex_match_id ("POSITIONAL"));
	      else if (lex_match_id ("ALPHA"))
		positional = 0;

	      if (lex_match (T_ALL) || token == '/' || token == '.')
		{
		  if (prev_nv != 0)
		    {
		      msg (SE, _("Cannot specify ALL after specifying a set "
			   "of variables."));
		      goto lossage;
		    }
		  fill_all_vars (&v, &nv, FV_NO_SYSTEM);
		}
	      else
		{
		  if (!lex_match ('('))
		    {
		      msg (SE, _("`(' expected on REORDER subcommand."));
		      free (v);
		      goto lossage;
		    }
		  if (!parse_variables (&default_dict, &v, &nv,
					PV_APPEND | PV_NO_DUPLICATE))
		    {
		      free (v);
		      goto lossage;
		    }
		  if (!lex_match (')'))
		    {
		      msg (SE, _("`)' expected following variable names on "
			   "REORDER subcommand."));
		      free (v);
		      goto lossage;
		    }
		}
	      qsort (&v[prev_nv], nv - prev_nv, sizeof *v, compare_variables);
	    }
	  while (token != '/' && token != '.');

	  if (nv != default_dict.nvar)
	    {
	      size_t nbytes = DIV_RND_UP (default_dict.nvar, 8);
	      unsigned char *bits = local_alloc (nbytes);
	      int i;

	      memset (bits, 0, nbytes);
	      for (i = 0; i < nv; i++)
		SET_BIT (bits, v[i]->index);
	      v = xrealloc (v, sizeof *v * default_dict.nvar);
	      for (i = 0; i < default_dict.nvar; i++)
		if (!TEST_BIT (bits, i))
		  v[nv++] = default_dict.var[i];
	      local_free (bits);
	    }

	  vm.reorder_list = v;
	}
      else if (lex_match_id ("RENAME"))
	{
	  if (already_encountered & 2)
	    {
	      msg (SE, _("RENAME subcommand may be given at most once."));
	      goto lossage;
	    }
	  already_encountered |= 2;

	  lex_match ('=');
	  do
	    {
	      int prev_nv_1 = vm.n_rename;
	      int prev_nv_2 = vm.n_rename;

	      if (!lex_match ('('))
		{
		  msg (SE, _("`(' expected on RENAME subcommand."));
		  goto lossage;
		}
	      if (!parse_variables (&default_dict, &vm.old_names, &vm.n_rename,
				    PV_APPEND | PV_NO_DUPLICATE))
		goto lossage;
	      if (!lex_match ('='))
		{
		  msg (SE, _("`=' expected between lists of new and old variable "
		       "names on RENAME subcommand."));
		  goto lossage;
		}
	      if (!parse_DATA_LIST_vars (&vm.new_names, &prev_nv_1, PV_APPEND))
		goto lossage;
	      if (prev_nv_1 != vm.n_rename)
		{
		  int i;

		  msg (SE, _("Differing number of variables in old name list "
		       "(%d) and in new name list (%d)."),
		       vm.n_rename - prev_nv_2, prev_nv_1 - prev_nv_2);
		  for (i = 0; i < prev_nv_1; i++)
		    free (&vm.new_names[i]);
		  free (&vm.new_names);
		  vm.new_names = NULL;
		  goto lossage;
		}
	      if (!lex_match (')'))
		{
		  msg (SE, _("`)' expected after variable lists on RENAME "
		       "subcommand."));
		  goto lossage;
		}
	    }
	  while (token != '.' && token != '/');
	}
      else if (lex_match_id ("KEEP"))
	{
	  struct variable **keep_vars;
	  int nv;
	  int counter;
	  int i;

	  if (already_encountered & 4)
	    {
	      msg (SE, _("KEEP subcommand may be given at most once.  It may not"
		   "be given in conjunction with the DROP subcommand."));
	      goto lossage;
	    }
	  already_encountered |= 4;

	  lex_match ('=');
	  if (!parse_variables (&default_dict, &keep_vars, &nv, PV_NONE))
	    goto lossage;

	  /* Transform the list of variables to keep into a list of
	     variables to drop.  First sort the keep list, then figure
	     out which variables are missing. */
	  forward = positional = 1;
	  qsort (keep_vars, nv, sizeof *keep_vars, compare_variables);

	  vm.n_drop = default_dict.nvar - nv;

	  counter = 0;
	  for (i = 0; i < nv; i++)
	    {
	      while (counter < keep_vars[i]->index)
		default_dict.var[counter++]->p.mfv.drop_this_var = 1;
	      default_dict.var[counter++]->p.mfv.drop_this_var = 0;
	    }
	  while (counter < nv)
	    default_dict.var[counter++]->p.mfv.drop_this_var = 1;

	  free (keep_vars);
	}
      else if (lex_match_id ("DROP"))
	{
	  struct variable **drop_vars;
	  int nv;
	  int i;

	  if (already_encountered & 4)
	    {
	      msg (SE, _("DROP subcommand may be given at most once.  It may not"
		   "be given in conjunction with the KEEP subcommand."));
	      goto lossage;
	    }
	  already_encountered |= 4;

	  lex_match ('=');
	  if (!parse_variables (&default_dict, &drop_vars, &nv, PV_NONE))
	    goto lossage;
	  for (i = 0; i < default_dict.nvar; i++)
	    default_dict.var[i]->p.mfv.drop_this_var = 0;
	  for (i = 0; i < nv; i++)
	    drop_vars[i]->p.mfv.drop_this_var = 1;
	  vm.n_drop = nv;
	  free (drop_vars);
	}
      else if (lex_match_id ("MAP"))
	{
	  struct dictionary *new_dict = rearrange_dict (&default_dict, &vm, 0);
	  if (!new_dict)
	    goto lossage;
	  /* FIXME: display new dictionary. */
	}
      else
	{
	  if (token == T_ID)
	    msg (SE, _("Unrecognized subcommand name `%s'."), tokid);
	  else
	    msg (SE, _("Subcommand name expected."));
	  goto lossage;
	}

      if (token == '.')
	break;
      if (token != '/')
	{
	  msg (SE, _("`/' or `.' expected."));
	  goto lossage;
	}
      lex_get ();
    }

  {
    int i;

    if (already_encountered & (1 | 4))
      {
	/* Read the data. */
	procedure (NULL, NULL, NULL);
      }

    if (NULL == rearrange_dict (&default_dict, &vm, 1))
      goto lossage;

    free (vm.reorder_list);
    free (vm.old_names);
    for (i = 0; i < vm.n_rename; i++)
      free (vm.new_names[i]);
    free (vm.new_names);

    return CMD_SUCCESS;
  }

lossage:
  {
    int i;

    free (vm.reorder_list);
    free (vm.old_names);
    for (i = 0; i < vm.n_rename; i++)
      free (vm.new_names[i]);
    free (vm.new_names);
    return CMD_FAILURE;
  }
}

/* Compares a pair of variables according to the settings in `forward'
   and `positional', returning a strcmp()-type result. */
static int
compare_variables (const void *pa, const void *pb)
{
  const struct variable *a = *(const struct variable **) pa;
  const struct variable *b = *(const struct variable **) pb;

  int result = positional ? a->index - b->index : strcmp (a->name, b->name);
  return forward ? result : -result;
}

/* (Possibly) rearranges variables and (possibly) removes some
   variables and (possibly) renames some more variables in dictionary
   D.  There are two modes of operation, distinguished by the value of
   PERMANENT:

   If PERMANENT is nonzero, then the dictionary is modified in place.
   Returns the new dictionary on success or NULL if there would have
   been duplicate variable names in the resultant dictionary (in this
   case the dictionary has not been modified).

   If PERMANENT is zero, then the dictionary is copied to a new
   dictionary structure that retains most of the same deep structure
   as D.  The p.mfv.new_name field of each variable is set to what
   would become the variable's new name if PERMANENT were nonzero.
   Returns the new dictionary. */
static struct dictionary *
rearrange_dict (struct dictionary * d, struct var_modification * vm, int permanent)
{
  struct dictionary *n;

  struct variable **save_var;

  /* Linked list of variables for deletion. */
  struct variable *head, *tail;

  int i;

  /* First decide what dictionary to modify. */
  if (permanent == 0)
    {
      n = xmalloc (sizeof *n);
      *n = *d;
    }
  else
    n = d;
  save_var = n->var;

  /* Perform first half of renaming. */
  if (permanent)
    {
      for (i = 0; i < d->nvar; i++)
	d->var[i]->p.mfv.new_name[0] = 0;
      d->var = xmalloc (sizeof *d->var * d->nvar);
    }
  else
    for (i = 0; i < d->nvar; i++)
      strcpy (d->var[i]->p.mfv.new_name, d->var[i]->name);
  for (i = 0; i < vm->n_rename; i++)
    strcpy (vm->old_names[i]->p.mfv.new_name, vm->new_names[i]);

  /* Copy the variable list, reordering if appropriate. */
  if (vm->reorder_list)
    memcpy (n->var, vm->reorder_list, sizeof *n->var * d->nvar);
  else if (!permanent)
    for (i = 0; i < d->nvar; i++)
      n->var[i] = d->var[i];

  /* Drop all the unwanted variables. */
  head = NULL;
  if (vm->n_drop)
    {
      int j;

      n->nvar = d->nvar - vm->n_drop;
      for (i = j = 0; i < n->nvar; i++)
	{
	  while (n->var[j]->p.mfv.drop_this_var != 0)
	    {
	      if (permanent)
		{
		  /* If this is permanent, then we have to keep a list
		     of all the dropped variables because they must be
		     free()'d, but can't be until we know that there
		     aren't any duplicate variable names. */
		  if (head)
		    tail = tail->p.mfv.next = n->var[j];
		  else
		    head = tail = n->var[j];
		}
	      j++;
	    }
	  n->var[i] = n->var[j++];
	}
      if (permanent)
	tail->p.mfv.next = NULL;
    }

  /* Check for duplicate variable names if appropriate. */
  if (permanent && vm->n_rename)
    {
      struct variable **v;

      if (vm->reorder_list)
	v = vm->reorder_list;	/* Reuse old buffer if possible. */
      else
	v = xmalloc (sizeof *v * n->nvar);
      memcpy (v, n->var, sizeof *v * n->nvar);
      forward = 1, positional = 0;
      qsort (v, n->nvar, sizeof *v, compare_variables);
      for (i = 1; i < n->nvar; i++)
	if (!strcmp (n->var[i]->name, n->var[i - 1]->name))
	  {
	    msg (SE, _("Duplicate variable name `%s' after renaming."),
		 n->var[i]->name);
	    if (vm->reorder_list == NULL)
	      free (v);
	    n->var = save_var;
	    return NULL;
	  }
      if (vm->reorder_list == NULL)
	free (v);
    }

  /* Delete unwanted variables and finalize renaming if
     appropriate. */
  if (permanent)
    {
      /* Delete dropped variables for good. */
      for (; head; head = tail)
	{
	  tail = head->p.mfv.next;
	  clear_variable (n, head);
	  free (head);
	}

      /* Remove names from all renamed variables. */
      head = NULL;
      for (i = 0; i < n->nvar; i++)
	if (n->var[i]->p.mfv.new_name[0])
	  {
	    avl_force_delete (n->var_by_name, n->var[i]);
	    if (head)
	      tail = tail->p.mfv.next = n->var[i];
	    else
	      head = tail = n->var[i];
	  }
      if (head)
	tail->p.mfv.next = NULL;

      /* Put names onto renamed variables. */
      for (; head; head = head->p.mfv.next)
	{
	  strcpy (head->name, head->p.mfv.new_name);
	  avl_force_insert (n->var_by_name, head);
	}
      free (save_var);

      /* As a final step the index fields must be redone. */
      for (i = 0; i < n->nvar; i++)
	n->var[i]->index = i;
    }

  return n;
}
