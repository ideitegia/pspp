/* PSPP - a program for statistical analysis.
   Copyright (C) 1997-9, 2000, 2010, 2011, 2012 Free Software Foundation, Inc.

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

#include <stdlib.h>

#include "data/dataset.h"
#include "data/dictionary.h"
#include "data/variable.h"
#include "language/command.h"
#include "language/lexer/lexer.h"
#include "language/lexer/variable-parser.h"
#include "libpspp/array.h"
#include "libpspp/assertion.h"
#include "libpspp/bit-vector.h"
#include "libpspp/compiler.h"
#include "libpspp/i18n.h"
#include "libpspp/message.h"
#include "libpspp/misc.h"
#include "libpspp/str.h"

#include "gl/xalloc.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

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
                                             const void *ordering);

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

static bool rearrange_dict (struct dictionary *d,
                           const struct var_modification *vm);

/* Performs MODIFY VARS command. */
int
cmd_modify_vars (struct lexer *lexer, struct dataset *ds)
{
  /* Bits indicated whether we've already encountered a subcommand of
     this type. */
  unsigned already_encountered = 0;

  /* What we are going to do to the active dataset. */
  struct var_modification vm;

  /* Return code. */
  int ret_code = CMD_CASCADING_FAILURE;

  size_t i;

  if (proc_make_temporary_transformations_permanent (ds))
    msg (SE, _("%s may not be used after %s.  "
               "Temporary transformations will be made permanent."), "MODIFY VARS", "TEMPORARY");

  vm.reorder_vars = NULL;
  vm.reorder_cnt = 0;
  vm.rename_vars = NULL;
  vm.new_names = NULL;
  vm.rename_cnt = 0;
  vm.drop_vars = NULL;
  vm.drop_cnt = 0;

  /* Parse each subcommand. */
  lex_match (lexer, T_SLASH);
  for (;;)
    {
      if (lex_match_id (lexer, "REORDER"))
	{
	  struct variable **v = NULL;
	  size_t nv = 0;

	  if (already_encountered & 1)
	    {
              lex_sbc_only_once ("REORDER");
	      goto done;
	    }
	  already_encountered |= 1;

	  lex_match (lexer, T_EQUALS);
	  do
	    {
              struct ordering ordering;
	      size_t prev_nv = nv;

	      ordering.forward = ordering.positional = 1;
	      if (lex_match_id (lexer, "FORWARD"));
	      else if (lex_match_id (lexer, "BACKWARD"))
		ordering.forward = 0;
	      if (lex_match_id (lexer, "POSITIONAL"));
	      else if (lex_match_id (lexer, "ALPHA"))
		ordering.positional = 0;

	      if (lex_match (lexer, T_ALL) || lex_token (lexer) == T_SLASH || lex_token (lexer) == T_ENDCMD)
		{
		  if (prev_nv != 0)
		    {
		      msg (SE, _("Cannot specify ALL after specifying a set "
			   "of variables."));
		      goto done;
		    }
		  dict_get_vars_mutable (dataset_dict (ds), &v, &nv, DC_SYSTEM);
		}
	      else
		{
		  if (!lex_match (lexer, T_LPAREN))
		    {
                      lex_error_expecting (lexer, "`('", NULL_SENTINEL);
		      free (v);
		      goto done;
		    }
		  if (!parse_variables (lexer, dataset_dict (ds), &v, &nv,
					PV_APPEND | PV_NO_DUPLICATE))
		    {
		      free (v);
		      goto done;
		    }
		  if (!lex_match (lexer, T_RPAREN))
		    {
                      lex_error_expecting (lexer, "`)'", NULL_SENTINEL);
		      free (v);
		      goto done;
		    }
		}
	      sort (&v[prev_nv], nv - prev_nv, sizeof *v,
                    compare_variables_given_ordering, &ordering);
	    }
	  while (lex_token (lexer) != T_SLASH
                 && lex_token (lexer) != T_ENDCMD);

	  vm.reorder_vars = v;
          vm.reorder_cnt = nv;
	}
      else if (lex_match_id (lexer, "RENAME"))
	{
	  if (already_encountered & 2)
	    {
              lex_sbc_only_once ("RENAME");
	      goto done;
	    }
	  already_encountered |= 2;

	  lex_match (lexer, T_EQUALS);
	  do
	    {
	      size_t prev_nv_1 = vm.rename_cnt;
	      size_t prev_nv_2 = vm.rename_cnt;

	      if (!lex_match (lexer, T_LPAREN))
		{
                  lex_error_expecting (lexer, "`('", NULL_SENTINEL);
		  goto done;
		}
	      if (!parse_variables (lexer, dataset_dict (ds),
				    &vm.rename_vars, &vm.rename_cnt,
				    PV_APPEND | PV_NO_DUPLICATE))
		goto done;
	      if (!lex_match (lexer, T_EQUALS))
		{
                  lex_error_expecting (lexer, "`='", NULL_SENTINEL);
		  goto done;
		}
	      if (!parse_DATA_LIST_vars (lexer, dataset_dict (ds),
                                         &vm.new_names, &prev_nv_1, PV_APPEND))
		goto done;
	      if (prev_nv_1 != vm.rename_cnt)
		{
		  msg (SE, _("Differing number of variables in old name list "
                             "(%zu) and in new name list (%zu)."),
		       vm.rename_cnt - prev_nv_2, prev_nv_1 - prev_nv_2);
		  for (i = 0; i < prev_nv_1; i++)
		    free (vm.new_names[i]);
		  free (vm.new_names);
		  vm.new_names = NULL;
		  goto done;
		}
	      if (!lex_match (lexer, T_RPAREN))
		{
                  lex_error_expecting (lexer, "`)'", NULL_SENTINEL);
		  goto done;
		}
	    }
	  while (lex_token (lexer) != T_ENDCMD
                 && lex_token (lexer) != T_SLASH);
	}
      else if (lex_match_id (lexer, "KEEP"))
	{
	  struct variable **keep_vars, **all_vars, **drop_vars;
	  size_t keep_cnt, all_cnt, drop_cnt;

	  if (already_encountered & 4)
	    {
	      msg (SE, _("%s subcommand may be given at most once.  It may "
			 "not be given in conjunction with the %s subcommand."),
		   "KEEP", "DROP");
	      goto done;
	    }
	  already_encountered |= 4;

	  lex_match (lexer, T_EQUALS);
	  if (!parse_variables (lexer, dataset_dict (ds), &keep_vars, &keep_cnt, PV_NONE))
	    goto done;

	  /* Transform the list of variables to keep into a list of
	     variables to drop.  First sort the keep list, then figure
	     out which variables are missing. */
	  sort (keep_vars, keep_cnt, sizeof *keep_vars,
                compare_variables_given_ordering, &forward_positional_ordering);

          dict_get_vars_mutable (dataset_dict (ds), &all_vars, &all_cnt, 0);
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
      else if (lex_match_id (lexer, "DROP"))
	{
	  struct variable **drop_vars;
	  size_t drop_cnt;

	  if (already_encountered & 4)
	    {
	      msg (SE, _("%s subcommand may be given at most once.  It may "
                         "not be given in conjunction with the %s "
                         "subcommand."),
		   "DROP", "KEEP"
		   );
	      goto done;
	    }
	  already_encountered |= 4;

	  lex_match (lexer, T_EQUALS);
	  if (!parse_variables (lexer, dataset_dict (ds), &drop_vars, &drop_cnt, PV_NONE))
	    goto done;
          vm.drop_vars = drop_vars;
          vm.drop_cnt = drop_cnt;
	}
      else if (lex_match_id (lexer, "MAP"))
	{
          struct dictionary *temp = dict_clone (dataset_dict (ds));
          int success = rearrange_dict (temp, &vm);
          if (success)
            {
              /* FIXME: display new dictionary. */
            }
          dict_destroy (temp);
	}
      else
	{
	  if (lex_token (lexer) == T_ID)
	    msg (SE, _("Unrecognized subcommand name `%s'."), lex_tokcstr (lexer));
	  else
	    msg (SE, _("Subcommand name expected."));
	  goto done;
	}

      if (lex_token (lexer) == T_ENDCMD)
	break;
      if (lex_token (lexer) != T_SLASH)
	{
          lex_error_expecting (lexer, "`/'", "`.'", NULL_SENTINEL);
	  goto done;
	}
      lex_get (lexer);
    }

  if (already_encountered & (1 | 4))
    {
      /* Read the data. */
      if (!proc_execute (ds))
        goto done;
    }

  if (!rearrange_dict (dataset_dict (ds), &vm))
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
                                  const void *ordering_)
{
  struct variable *const *pa = a_;
  struct variable *const *pb = b_;
  const struct variable *a = *pa;
  const struct variable *b = *pb;
  const struct ordering *ordering = ordering_;

  int result;
  if (ordering->positional)
    {
      size_t a_index = var_get_dict_index (a);
      size_t b_index = var_get_dict_index (b);
      result = a_index < b_index ? -1 : a_index > b_index;
    }
  else
    result = utf8_strcasecmp (var_get_name (a), var_get_name (b));
  if (!ordering->forward)
    result = -result;
  return result;
}

/* Pairs a variable with a new name. */
struct var_renaming
  {
    struct variable *var;
    const char *new_name;
  };

/* A algo_compare_func that compares new_name members in struct
   var_renaming structures A and B. */
static int
compare_var_renaming_by_new_name (const void *a_, const void *b_,
                                  const void *aux UNUSED)
{
  const struct var_renaming *a = a_;
  const struct var_renaming *b = b_;

  return utf8_strcasecmp (a->new_name, b->new_name);
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
  dict_get_vars_mutable (d, &all_vars, &all_cnt, 0);

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
      var_renaming[i].new_name = var_get_name (keep_vars[i]);
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

      vr->new_name = vm->new_names[i];
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
   according to VM.  Returns true if successful, false if there
   would have been duplicate variable names if the modifications
   had been carried out.  In the latter case, the dictionary is
   not modified. */
static bool
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
    return false;

  /* Record the old names of variables to rename.  After
     variables are deleted, we can't depend on the variables to
     still exist, but we can still look them up by name. */
  rename_old_names = xnmalloc (vm->rename_cnt, sizeof *rename_old_names);
  for (i = 0; i < vm->rename_cnt; i++)
    rename_old_names[i] = xstrdup (var_get_name (vm->rename_vars[i]));

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

  return true;
}
