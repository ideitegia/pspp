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

#include <stdlib.h>

#include <data/dictionary.h>
#include <data/procedure.h>
#include <data/variable.h>
#include <language/command.h>
#include <language/lexer/lexer.h>
#include <language/lexer/variable-parser.h>
#include <libpspp/alloc.h>
#include <libpspp/assertion.h>
#include <libpspp/array.h>
#include <libpspp/bit-vector.h>
#include <libpspp/compiler.h>
#include <libpspp/hash.h>
#include <libpspp/message.h>
#include <libpspp/message.h>
#include <libpspp/misc.h>
#include <libpspp/str.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* FIXME: should change weighting variable, etc. */
/* These control the ordering produced by
   compare_variables_given_ordering(). */
struct ordering
  {
    int forward;		/* 1=FORWARD, 0=BACKWARD. */
    int positional;		/* 1=POSITIONAL, 0=ALPHA. */
  };

/* Increasing order of variable index. */
static struct ordering forward_positional_ordering = {1, 1};

static int compare_variables_given_ordering (const void *, const void *,
                                             void *ordering);

/* Explains how to modify the variables in a dictionary. */
struct var_modification
  {
    /* New variable ordering. */
    struct variable **reorder_vars;
    size_t reorder_cnt;

    /* DROP/KEEP information. */
    struct variable **drop_vars;
    size_t drop_cnt;

    /* New variable names. */
    struct variable **rename_vars;
    char **new_names;
    size_t rename_cnt;
  };

static int rearrange_dict (struct dictionary *d,
                           const struct var_modification *vm);

/* Performs MODIFY VARS command. */
int
cmd_modify_vars (void)
{
  /* Bits indicated whether we've already encountered a subcommand of
     this type. */
  unsigned already_encountered = 0;

  /* What we're gonna do to the active file. */
  struct var_modification vm;

  /* Return code. */
  int ret_code = CMD_CASCADING_FAILURE;

  size_t i;

  if (proc_make_temporary_transformations_permanent ())
    msg (SE, _("MODIFY VARS may not be used after TEMPORARY.  "
               "Temporary transformations will be made permanent."));

  vm.reorder_vars = NULL;
  vm.reorder_cnt = 0;
  vm.rename_vars = NULL;
  vm.new_names = NULL;
  vm.rename_cnt = 0;
  vm.drop_vars = NULL;
  vm.drop_cnt = 0;

  /* Parse each subcommand. */
  lex_match ('/');
  for (;;)
    {
      if (lex_match_id ("REORDER"))
	{
	  struct variable **v = NULL;
	  size_t nv = 0;

	  if (already_encountered & 1)
	    {
	      msg (SE, _("REORDER subcommand may be given at most once."));
	      goto done;
	    }
	  already_encountered |= 1;

	  lex_match ('=');
	  do
	    {
              struct ordering ordering;
	      size_t prev_nv = nv;

	      ordering.forward = ordering.positional = 1;
	      if (lex_match_id ("FORWARD"));
	      else if (lex_match_id ("BACKWARD"))
		ordering.forward = 0;
	      if (lex_match_id ("POSITIONAL"));
	      else if (lex_match_id ("ALPHA"))
		ordering.positional = 0;

	      if (lex_match (T_ALL) || token == '/' || token == '.')
		{
		  if (prev_nv != 0)
		    {
		      msg (SE, _("Cannot specify ALL after specifying a set "
			   "of variables."));
		      goto done;
		    }
		  dict_get_vars (default_dict, &v, &nv, 1u << DC_SYSTEM);
		}
	      else
		{
		  if (!lex_match ('('))
		    {
		      msg (SE, _("`(' expected on REORDER subcommand."));
		      free (v);
		      goto done;
		    }
		  if (!parse_variables (default_dict, &v, &nv,
					PV_APPEND | PV_NO_DUPLICATE))
		    {
		      free (v);
		      goto done;
		    }
		  if (!lex_match (')'))
		    {
		      msg (SE, _("`)' expected following variable names on "
			   "REORDER subcommand."));
		      free (v);
		      goto done;
		    }
		}
	      sort (&v[prev_nv], nv - prev_nv, sizeof *v,
                    compare_variables_given_ordering, &ordering);
	    }
	  while (token != '/' && token != '.');

	  vm.reorder_vars = v;
          vm.reorder_cnt = nv;
	}
      else if (lex_match_id ("RENAME"))
	{
	  if (already_encountered & 2)
	    {
	      msg (SE, _("RENAME subcommand may be given at most once."));
	      goto done;
	    }
	  already_encountered |= 2;

	  lex_match ('=');
	  do
	    {
	      size_t prev_nv_1 = vm.rename_cnt;
	      size_t prev_nv_2 = vm.rename_cnt;

	      if (!lex_match ('('))
		{
		  msg (SE, _("`(' expected on RENAME subcommand."));
		  goto done;
		}
	      if (!parse_variables (default_dict, &vm.rename_vars, &vm.rename_cnt,
				    PV_APPEND | PV_NO_DUPLICATE))
		goto done;
	      if (!lex_match ('='))
		{
		  msg (SE, _("`=' expected between lists of new and old variable "
		       "names on RENAME subcommand."));
		  goto done;
		}
	      if (!parse_DATA_LIST_vars (&vm.new_names, &prev_nv_1, PV_APPEND))
		goto done;
	      if (prev_nv_1 != vm.rename_cnt)
		{
		  msg (SE, _("Differing number of variables in old name list "
		       "(%d) and in new name list (%d)."),
		       vm.rename_cnt - prev_nv_2, prev_nv_1 - prev_nv_2);
		  for (i = 0; i < prev_nv_1; i++)
		    free (vm.new_names[i]);
		  free (vm.new_names);
		  vm.new_names = NULL;
		  goto done;
		}
	      if (!lex_match (')'))
		{
		  msg (SE, _("`)' expected after variable lists on RENAME "
		       "subcommand."));
		  goto done;
		}
	    }
	  while (token != '.' && token != '/');
	}
      else if (lex_match_id ("KEEP"))
	{
	  struct variable **keep_vars, **all_vars, **drop_vars;
	  size_t keep_cnt, all_cnt, drop_cnt;

	  if (already_encountered & 4)
	    {
	      msg (SE, _("KEEP subcommand may be given at most once.  It may not"
		   "be given in conjunction with the DROP subcommand."));
	      goto done;
	    }
	  already_encountered |= 4;

	  lex_match ('=');
	  if (!parse_variables (default_dict, &keep_vars, &keep_cnt, PV_NONE))
	    goto done;

	  /* Transform the list of variables to keep into a list of
	     variables to drop.  First sort the keep list, then figure
	     out which variables are missing. */
	  sort (keep_vars, keep_cnt, sizeof *keep_vars,
                compare_variables_given_ordering, &forward_positional_ordering);

          dict_get_vars (default_dict, &all_vars, &all_cnt, 0);
          assert (all_cnt >= keep_cnt);

          drop_cnt = all_cnt - keep_cnt;
          drop_vars = xnmalloc (drop_cnt, sizeof *keep_vars);
          if (set_difference (all_vars, all_cnt,
                              keep_vars, keep_cnt,
                              sizeof *all_vars,
                              drop_vars,
                              compare_variables_given_ordering,
                              &forward_positional_ordering)
              != drop_cnt)
            NOT_REACHED ();

          free (keep_vars);
          free (all_vars);

          vm.drop_vars = drop_vars;
          vm.drop_cnt = drop_cnt;
	}
      else if (lex_match_id ("DROP"))
	{
	  struct variable **drop_vars;
	  size_t drop_cnt;

	  if (already_encountered & 4)
	    {
	      msg (SE, _("DROP subcommand may be given at most once.  It may "
                         "not be given in conjunction with the KEEP "
                         "subcommand."));
	      goto done;
	    }
	  already_encountered |= 4;

	  lex_match ('=');
	  if (!parse_variables (default_dict, &drop_vars, &drop_cnt, PV_NONE))
	    goto done;
          vm.drop_vars = drop_vars;
          vm.drop_cnt = drop_cnt;
	}
      else if (lex_match_id ("MAP"))
	{
          struct dictionary *temp = dict_clone (default_dict);
          int success = rearrange_dict (temp, &vm);
          if (success) 
            {
              /* FIXME: display new dictionary. */ 
            }
          dict_destroy (temp);
	}
      else
	{
	  if (token == T_ID)
	    msg (SE, _("Unrecognized subcommand name `%s'."), tokid);
	  else
	    msg (SE, _("Subcommand name expected."));
	  goto done;
	}

      if (token == '.')
	break;
      if (token != '/')
	{
	  msg (SE, _("`/' or `.' expected."));
	  goto done;
	}
      lex_get ();
    }

  if (already_encountered & (1 | 4))
    {
      /* Read the data. */
      if (!procedure (NULL, NULL)) 
        goto done; 
    }

  if (!rearrange_dict (default_dict, &vm))
    goto done; 

  ret_code = CMD_SUCCESS;

done:
  free (vm.reorder_vars);
  free (vm.rename_vars);
  for (i = 0; i < vm.rename_cnt; i++)
    free (vm.new_names[i]);
  free (vm.new_names);
  free (vm.drop_vars);
  return ret_code;
}

/* Compares A and B according to the settings in
   ORDERING, returning a strcmp()-type result. */
static int
compare_variables_given_ordering (const void *a_, const void *b_,
                                  void *ordering_)
{
  struct variable *const *pa = a_;
  struct variable *const *pb = b_;
  const struct variable *a = *pa;
  const struct variable *b = *pb;
  const struct ordering *ordering = ordering_;

  int result;
  if (ordering->positional)
    result = a->index < b->index ? -1 : a->index > b->index;
  else
    result = strcasecmp (a->name, b->name);
  if (!ordering->forward)
    result = -result;
  return result;
}

/* Pairs a variable with a new name. */
struct var_renaming
  {
    struct variable *var;
    char new_name[LONG_NAME_LEN + 1];
  };

/* A algo_compare_func that compares new_name members in struct
   var_renaming structures A and B. */
static int
compare_var_renaming_by_new_name (const void *a_, const void *b_,
                                  void *foo UNUSED) 
{
  const struct var_renaming *a = a_;
  const struct var_renaming *b = b_;

  return strcasecmp (a->new_name, b->new_name);
}

/* Returns true if performing VM on dictionary D would not cause
   problems such as duplicate variable names.  Returns false
   otherwise, and issues an error message. */
static int
validate_var_modification (const struct dictionary *d,
                           const struct var_modification *vm) 
{
  /* Variable reordering can't be a problem, so we don't simulate
     it.  Variable renaming can cause duplicate names, but
     dropping variables can eliminate them, so we simulate both
     of those. */
  struct variable **all_vars;
  struct variable **keep_vars;
  struct variable **drop_vars;
  size_t keep_cnt, drop_cnt;
  size_t all_cnt;

  struct var_renaming *var_renaming;
  int valid;
  size_t i;

  /* All variables, in index order. */
  dict_get_vars (d, &all_vars, &all_cnt, 0);

  /* Drop variables, in index order. */
  drop_cnt = vm->drop_cnt;
  drop_vars = xnmalloc (drop_cnt, sizeof *drop_vars);
  memcpy (drop_vars, vm->drop_vars, drop_cnt * sizeof *drop_vars);
  sort (drop_vars, drop_cnt, sizeof *drop_vars,
        compare_variables_given_ordering, &forward_positional_ordering);

  /* Keep variables, in index order. */
  assert (all_cnt >= drop_cnt);
  keep_cnt = all_cnt - drop_cnt;
  keep_vars = xnmalloc (keep_cnt, sizeof *keep_vars);
  if (set_difference (all_vars, all_cnt,
                      drop_vars, drop_cnt,
                      sizeof *all_vars,
                      keep_vars,
                      compare_variables_given_ordering,
                      &forward_positional_ordering) != keep_cnt)
    NOT_REACHED ();

  /* Copy variables into var_renaming array. */
  var_renaming = xnmalloc (keep_cnt, sizeof *var_renaming);
  for (i = 0; i < keep_cnt; i++) 
    {
      var_renaming[i].var = keep_vars[i];
      strcpy (var_renaming[i].new_name, keep_vars[i]->name);
    }
  
  /* Rename variables in var_renaming array. */
  for (i = 0; i < vm->rename_cnt; i++) 
    {
      struct variable *const *kv;
      struct var_renaming *vr;

      /* Get the var_renaming element. */
      kv = binary_search (keep_vars, keep_cnt, sizeof *keep_vars,
                          &vm->rename_vars[i],
                          compare_variables_given_ordering,
                          &forward_positional_ordering);
      if (kv == NULL)
        continue;
      vr = var_renaming + (kv - keep_vars);

      strcpy (vr->new_name, vm->new_names[i]);
    }

  /* Sort var_renaming array by new names and check for
     duplicates. */
  sort (var_renaming, keep_cnt, sizeof *var_renaming,
        compare_var_renaming_by_new_name, NULL);
  valid = adjacent_find_equal (var_renaming, keep_cnt, sizeof *var_renaming,
                               compare_var_renaming_by_new_name, NULL) == NULL;

  /* Clean up. */
  free (all_vars);
  free (keep_vars);
  free (drop_vars);
  free (var_renaming);

  return valid;
}

/* Reoders, removes, and renames variables in dictionary D
   according to VM.  Returns nonzero if successful, zero if there
   would have been duplicate variable names if the modifications
   had been carried out.  In the latter case, the dictionary is
   not modified. */
static int
rearrange_dict (struct dictionary *d, const struct var_modification *vm)
{
  char **rename_old_names;

  struct variable **rename_vars;
  char **rename_new_names;
  size_t rename_cnt;

  size_t i;

  /* Check whether the modifications will cause duplicate
     names. */
  if (!validate_var_modification (d, vm))
    return 0;

  /* Record the old names of variables to rename.  After
     variables are deleted, we can't depend on the variables to
     still exist, but we can still look them up by name. */
  rename_old_names = xnmalloc (vm->rename_cnt, sizeof *rename_old_names);
  for (i = 0; i < vm->rename_cnt; i++)
    rename_old_names[i] = xstrdup (vm->rename_vars[i]->name);

  /* Reorder and delete variables. */
  dict_reorder_vars (d, vm->reorder_vars, vm->reorder_cnt);
  dict_delete_vars (d, vm->drop_vars, vm->drop_cnt);

  /* Compose lists of variables to rename and their new names. */
  rename_vars = xnmalloc (vm->rename_cnt, sizeof *rename_vars);
  rename_new_names = xnmalloc (vm->rename_cnt, sizeof *rename_new_names);
  rename_cnt = 0;
  for (i = 0; i < vm->rename_cnt; i++)
    {
      struct variable *var = dict_lookup_var (d, rename_old_names[i]);
      if (var == NULL)
        continue;
      
      rename_vars[rename_cnt] = var;
      rename_new_names[rename_cnt] = vm->new_names[i];
      rename_cnt++;
    }

  /* Do renaming. */
  if (dict_rename_vars (d, rename_vars, rename_new_names, rename_cnt,
                        NULL) == 0)
    NOT_REACHED ();

  /* Clean up. */
  for (i = 0; i < vm->rename_cnt; i++)
    free (rename_old_names[i]);
  free (rename_old_names);
  free (rename_vars);
  free (rename_new_names);

  return 1;
}
